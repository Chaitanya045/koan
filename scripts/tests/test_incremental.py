#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/ci/incremental.sh, on a throwaway repository.

The repository holds a copy of the script, a patches/ directory and a fake
chromium/src. A base archive is made from it as scripts/ci/mac_archive.sh
pack-source does (GNU tar, POSIX format, zstd), then the repository moves on
to a second commit and the script turns the unpacked base into that commit.
Skipped when git, bash, zstd or GNU tar is missing.
"""

import difflib
import filecmp
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts' / 'ci' / 'incremental.sh'

OLD = 1_000_000_000  # The base's mtimes: far from "now".
TOOLCHAIN = 'Xcode 27.0, SDK 27.0'
INPUTS = ('CHROMIUM_VERSION branding scripts/branding.py '
          'scripts/ci/mac_fetch.sh')

TRACKED = {
    '.gitignore': '/chromium\n/depot_tools\n/.cache/\n',
    'CHROMIUM_VERSION': '158.0.8127.18\n',
    'branding/logo.svg': '<svg/>\n',
    'branding/mac/icon.txt': 'icon\n',
    'scripts/branding.py': 'print("branding")\n',
    'scripts/ci/mac_fetch.sh': '# fetch v1\n',
    'src/foo.cc': 'foo\n',
    'src/bar.cc': 'bar\n',
    'src/gone.cc': 'gone\n',
}
PRISTINE = {
    'f1.txt': 'one\ntwo\nthree\n',
    'f2.txt': 'red\ngreen\nblue\n',
    'f3.txt': 'a\nb\nc\n',
    'dir/f4.txt': 'x\ny\nz\n',
}
# patch path (under patches/) -> (file in chromium/src, its new content)
PATCHES = {
    'a.patch': ('f1.txt', 'one\nTWO\nthree\n'),
    'ui/b.patch': ('dir/f4.txt', 'x\nY\nz\n'),
}


def _have(*tools):
  return all(shutil.which(t) for t in tools)


def _gnu_tar():
  for name in ('gtar', 'tar'):
    path = shutil.which(name)
    if path and b'GNU tar' in subprocess.run(
        [path, '--version'], capture_output=True).stdout:
      return path
  return None


def make_patch(name, old, new):
  """A git-style patch turning `old` into `new` in the file `name`."""
  body = ''.join(difflib.unified_diff(
      old.splitlines(True), new.splitlines(True),
      f'a/{name}', f'b/{name}'))
  return f'diff --git a/{name} b/{name}\n{body}'


@unittest.skipUnless(_have('git', 'bash', 'zstd') and _gnu_tar(),
                     'needs git, bash, zstd and GNU tar')
class IncrementalTest(unittest.TestCase):

  def setUp(self):
    tmp = tempfile.TemporaryDirectory()
    self.addCleanup(tmp.cleanup)
    self.tmp = pathlib.Path(tmp.name).resolve()
    self.repo = self.tmp / 'repo'
    self.scratch = self.tmp / 'scratch'
    self.archive = self.tmp / 'base-source.tar.zst'
    self.repo.mkdir()
    self.env = {
        'PATH': os.environ['PATH'],
        'HOME': str(self.tmp),
        'GIT_CONFIG_GLOBAL': os.devnull,
        'GIT_CONFIG_SYSTEM': os.devnull,
        'GIT_AUTHOR_NAME': 'Test', 'GIT_AUTHOR_EMAIL': 'test@example.com',
        'GIT_COMMITTER_NAME': 'Test', 'GIT_COMMITTER_EMAIL': 'test@example.com',
        # chromium/src has no .git: keep git from using the repo's (as the
        # script does).
        'GIT_CEILING_DIRECTORIES': str(self.repo),
    }
    self.git('init', '-q')
    (self.repo / 'scripts/ci').mkdir(parents=True)
    shutil.copy(SCRIPT, self.repo / 'scripts/ci/incremental.sh')
    for name, text in TRACKED.items():
      self.write(name, text)
    for name, (target, new) in PATCHES.items():
      self.write(f'patches/{name}', make_patch(target, PRISTINE[target], new))
    self.commit('base')
    self.make_base()
    self.write_pristine(self.tmp / 'pristine')

  # --- helpers -------------------------------------------------------------

  def git(self, *args, cwd=None):
    return subprocess.run(
        ['git', '-c', 'commit.gpgsign=false', *args], cwd=cwd or self.repo,
        env=self.env, check=True, capture_output=True, text=True).stdout

  def write(self, name, text):
    path = self.repo / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)

  def remove(self, name):
    (self.repo / name).unlink()

  def commit(self, message):
    self.git('add', '-A')
    self.git('commit', '-q', '-m', message)

  def apply_patches(self, src, patches_dir):
    for patch in sorted(patches_dir.rglob('*.patch')):
      subprocess.run(['git', '-C', str(src), 'apply', str(patch)],
                     env=self.env, check=True, capture_output=True)

  def write_pristine(self, path):
    for name, text in PRISTINE.items():
      (path / name).parent.mkdir(parents=True, exist_ok=True)
      (path / name).write_text(text)

  def make_base(self):
    """What the fetch left, packed as scripts/ci/mac_archive.sh does."""
    src = self.repo / 'chromium/src'
    self.write_pristine(src)
    self.apply_patches(src, self.repo / 'patches')
    (self.repo / '.cache').mkdir()
    (self.repo / '.cache/ci-toolchain').write_text(TOOLCHAIN + '\n')
    for path in self.repo.rglob('*'):
      if '.git' not in path.relative_to(self.repo).parts[:1]:
        os.utime(path, (OLD, OLD), follow_symlinks=False)
    self.pack(self.repo, '.git')
    # A new runner: a fresh checkout of the next commit, none of the fetch.
    shutil.rmtree(self.repo / 'chromium')
    shutil.rmtree(self.repo / '.cache')
    for name in self.git('ls-files', '-z').split('\0')[:-1]:
      os.utime(self.repo / name, None)

  def pack(self, top, skip=None):
    """The archive of top's entries but `skip`, as pack-source makes it."""
    entries = [f'./{p.name}' for p in sorted(top.iterdir()) if p.name != skip]
    tarball = self.tmp / 'base.tar'
    subprocess.run([_gnu_tar(), '--format=posix', '-C', str(top), '-cf',
                    str(tarball), *entries], check=True)
    subprocess.run(['zstd', '-q', '-f', '-o', str(self.archive), str(tarball)],
                   check=True)
    tarball.unlink()

  def run_script(self, inputs=INPUTS, toolchain=TOOLCHAIN):
    env = dict(self.env, KOAN_TOOLCHAIN=toolchain,
               KOAN_BASE_ARCHIVE=str(self.archive))
    if inputs is not None:
      env['KOAN_FULL_BUILD_INPUTS'] = inputs
    return subprocess.run(
        ['bash', str(self.repo / 'scripts/ci/incremental.sh'), '1',
         'koan-mac', str(self.scratch)],
        cwd=self.repo, env=env, capture_output=True, text=True)

  def assert_ok(self, result):
    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

  def assert_full_build(self, result):
    self.assertEqual(result.returncode, 3, result.stdout + result.stderr)

  def mtime(self, name):
    return (self.repo / name).stat().st_mtime_ns

  def assert_kept(self, *names):
    for name in names:
      self.assertEqual(self.mtime(name), OLD * 10**9, f'{name} was touched')

  def assert_new(self, *names):
    for name in names:
      self.assertGreater(self.mtime(name), OLD * 10**9 + 10**9,
                         f'{name} kept the base mtime')

  def assert_src_is_pristine_plus_patches(self):
    """chromium/src equals the pristine sources with this commit's patches."""
    expected = self.tmp / 'expected'
    shutil.copytree(self.tmp / 'pristine', expected)
    self.apply_patches(expected, self.repo / 'patches')

    def walk(top):
      return sorted(str(p.relative_to(top)) for p in top.rglob('*')
                    if p.is_file())
    actual = self.repo / 'chromium/src'
    self.assertEqual(walk(actual), walk(expected))
    for name in walk(expected):
      self.assertTrue(filecmp.cmp(actual / name, expected / name,
                                  shallow=False), name)

  def assert_tree_is_head(self):
    self.assertEqual(self.git('status', '--porcelain', '--untracked-files=all'),
                     '')

  # --- the tracked tree ----------------------------------------------------

  def test_nothing_changed_keeps_every_mtime(self):
    self.assert_ok(self.run_script())
    self.assert_tree_is_head()
    self.assert_kept('src/foo.cc', 'src/bar.cc', 'branding/logo.svg',
                     'patches/a.patch', 'chromium/src/f1.txt',
                     'chromium/src/f2.txt', 'chromium/src/dir/f4.txt',
                     '.cache/ci-toolchain')
    self.assert_src_is_pristine_plus_patches()

  def test_changed_added_and_deleted_files(self):
    self.write('src/foo.cc', 'foo, changed\n')
    self.write('src/new.cc', 'new\n')
    self.write('src/sub/deeper.cc', 'deeper\n')
    self.remove('src/gone.cc')
    self.commit('files')
    self.assert_ok(self.run_script())
    self.assert_tree_is_head()
    self.assertEqual((self.repo / 'src/foo.cc').read_text(), 'foo, changed\n')
    self.assertFalse((self.repo / 'src/gone.cc').exists())
    self.assert_new('src/foo.cc', 'src/new.cc', 'src/sub/deeper.cc')
    self.assert_kept('src/bar.cc', 'chromium/src/f1.txt')
    self.assert_src_is_pristine_plus_patches()

  # --- patches -------------------------------------------------------------

  def test_changed_patch(self):
    self.write('patches/a.patch',
               make_patch('f1.txt', PRISTINE['f1.txt'], 'ONE\ntwo\nthree\n'))
    self.commit('patch')
    self.assert_ok(self.run_script())
    self.assertEqual((self.repo / 'chromium/src/f1.txt').read_text(),
                     'ONE\ntwo\nthree\n')
    self.assert_new('chromium/src/f1.txt')
    self.assert_kept('chromium/src/dir/f4.txt', 'chromium/src/f2.txt')
    self.assert_src_is_pristine_plus_patches()

  def test_added_patch(self):
    self.write('patches/ui/c.patch',
               make_patch('f2.txt', PRISTINE['f2.txt'], 'red\nGREEN\nblue\n'))
    self.commit('patch')
    self.assert_ok(self.run_script())
    self.assertEqual((self.repo / 'chromium/src/f2.txt').read_text(),
                     'red\nGREEN\nblue\n')
    self.assert_new('chromium/src/f2.txt')
    self.assert_kept('chromium/src/f1.txt', 'chromium/src/dir/f4.txt')
    self.assert_src_is_pristine_plus_patches()

  def test_removed_patch(self):
    self.remove('patches/a.patch')
    self.commit('patch')
    self.assert_ok(self.run_script())
    self.assertEqual((self.repo / 'chromium/src/f1.txt').read_text(),
                     PRISTINE['f1.txt'])
    self.assert_new('chromium/src/f1.txt')
    self.assert_kept('chromium/src/dir/f4.txt')
    self.assert_src_is_pristine_plus_patches()

  def test_patches_changed_added_and_removed_together(self):
    self.write('patches/a.patch',
               make_patch('f1.txt', PRISTINE['f1.txt'], 'one\ntwo\nTHREE\n'))
    self.remove('patches/ui/b.patch')
    self.write('patches/c.patch',
               make_patch('f3.txt', PRISTINE['f3.txt'], 'a\nB\nc\n'))
    self.write('src/foo.cc', 'foo, changed\n')
    self.commit('everything')
    self.assert_ok(self.run_script())
    self.assert_tree_is_head()
    self.assert_src_is_pristine_plus_patches()
    self.assert_kept('chromium/src/f2.txt', 'src/bar.cc')

  def test_a_patch_that_no_longer_reverses_is_a_full_build(self):
    # The base's tree isn't what its patches say: the old patch can't come out.
    work = self.tmp / 'broken'
    work.mkdir()
    subprocess.run(
        f'zstd -dc -q "{self.archive}" | "{_gnu_tar()}" -C "{work}" -xf -',
        shell=True, check=True)
    (work / 'chromium/src/f1.txt').write_text('something else\n')
    self.pack(work)
    self.write('patches/a.patch',
               make_patch('f1.txt', PRISTINE['f1.txt'], 'ONE\ntwo\nthree\n'))
    self.commit('patch')
    self.assert_full_build(self.run_script())

  # --- when the base can't be used -----------------------------------------

  def test_new_chromium_is_a_full_build(self):
    self.write('CHROMIUM_VERSION', '159.0.0.1\n')
    self.commit('chromium')
    self.assert_full_build(self.run_script())

  def full_build_after(self, name, text=None):
    """Commits a change to `name` (removing it if text is None) and runs."""
    if text is None:
      self.remove(name)
    else:
      self.write(name, text)
    self.commit('inputs')
    self.assert_full_build(self.run_script())

  def test_changed_branding_file(self):
    self.full_build_after('branding/logo.svg', '<svg id="2"/>\n')

  def test_added_branding_file(self):
    self.full_build_after('branding/mac/new.txt', 'new\n')

  def test_removed_branding_file(self):
    self.full_build_after('branding/mac/icon.txt')

  def test_changed_fetch_script(self):
    self.full_build_after('scripts/ci/mac_fetch.sh', '# fetch v2\n')

  def test_changed_branding_script(self):
    self.full_build_after('scripts/branding.py', 'print(2)\n')

  def test_a_changed_file_outside_the_inputs_is_not(self):
    self.write('scripts/other.sh', '# not an input\n')
    self.write('src/foo.cc', 'foo, changed\n')
    self.commit('other')
    self.assert_ok(self.run_script())

  def test_the_inputs_decide(self):
    # The same change is a full build only when its path is listed.
    self.write('scripts/ci/mac_fetch.sh', '# fetch v2\n')
    self.commit('script')
    self.assert_full_build(self.run_script())
    self.assert_ok(self.run_script(inputs='CHROMIUM_VERSION branding'))

  def test_other_toolchain_is_a_full_build(self):
    self.assert_full_build(self.run_script(toolchain='Xcode 28.0, SDK 28.0'))

  def test_missing_inputs_variable_is_an_error(self):
    result = self.run_script(inputs=None)
    self.assertNotIn(result.returncode, (0, 3), result.stdout + result.stderr)


if __name__ == '__main__':
  unittest.main()
