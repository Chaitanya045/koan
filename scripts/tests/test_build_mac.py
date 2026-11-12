#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for the friends' Mac build: scripts/koan_prereqs.sh's judgements
and scripts/build.sh --dry-run's plan (what it resumes, when it updates).

Runs on Linux: the Mac probes are faked with KOAN_FAKE_*.
"""

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
PREREQS = ROOT / 'scripts' / 'koan_prereqs.sh'

GOOD_MAC = {
    'KOAN_FAKE_OS': 'mac',
    'KOAN_FAKE_ARCH': 'arm64',
    'KOAN_FAKE_MACOS': '27.0',
    'KOAN_FAKE_XCODE': 'Xcode 27.1',
    'KOAN_FAKE_XCODE_LICENSE': '0',
    'KOAN_FAKE_METAL': '/fake/metal',
    'KOAN_FAKE_DISK_GB': '200',
    'KOAN_FAKE_RAM_GB': '16',
    'JOBS': '3',
}


def judge(check, *args):
  """Runs koan_judge_<check> <args>: (exit code, output)."""
  r = subprocess.run(
      ['bash', '-c', f'source "{PREREQS}"; koan_judge_{check} "$@"', 'x',
       *args],
      capture_output=True, text=True, check=False)
  return r.returncode, r.stdout.strip()


class JudgeTest(unittest.TestCase):

  def assertVerdict(self, verdict, check, *args):
    rc, out = judge(check, *args)
    self.assertTrue(out.startswith(verdict), out)
    self.assertEqual(rc, 1 if verdict == 'FAIL' else 0, out)

  def test_arch(self):
    self.assertVerdict('ok', 'arch', 'arm64')
    self.assertVerdict('FAIL', 'arch', 'x86_64')

  def test_macos(self):
    self.assertVerdict('ok', 'macos', '27.0.1')
    self.assertVerdict('warn', 'macos', '26.2')
    self.assertVerdict('FAIL', 'macos', '14.7')
    self.assertVerdict('FAIL', 'macos', '')

  def test_xcode(self):
    self.assertVerdict('ok', 'xcode', '/X', 'Xcode 27.1')
    self.assertVerdict('warn', 'xcode', '/X', 'Xcode 26.3')
    self.assertVerdict('FAIL', 'xcode', '/X', 'Xcode 16.4')
    # Only the Command Line Tools: no xcodebuild -version.
    rc, out = judge('xcode', '/Library/Developer/CommandLineTools', '')
    self.assertEqual(rc, 1)
    self.assertIn('DEVELOPER_DIR', out)

  def test_licence_and_metal(self):
    self.assertVerdict('ok', 'xcode_license', '0')
    self.assertVerdict('FAIL', 'xcode_license', '69')
    self.assertVerdict('ok', 'metal', '/x/metal')
    rc, out = judge('metal', '')
    self.assertEqual(rc, 1)
    self.assertIn('-downloadComponent MetalToolchain', out)

  def test_disk_needs_less_with_a_checkout(self):
    self.assertVerdict('FAIL', 'disk', '60', '0')
    self.assertVerdict('ok', 'disk', '60', '1')
    self.assertVerdict('warn', 'disk', '110', '0')
    self.assertVerdict('ok', 'disk', '130', '0')

  def test_ram(self):
    self.assertVerdict('FAIL', 'ram', '4')
    self.assertVerdict('warn', 'ram', '8')
    self.assertVerdict('ok', 'ram', '24')

  def test_path_with_space(self):
    self.assertVerdict('FAIL', 'path', '/Users/<user>/My Code/koan')
    self.assertVerdict('ok', 'path', '/Users/<user>/src/koan')


class DryRunTest(unittest.TestCase):
  """build.sh --dry-run against a scratch clone (scripts, args, pin)."""

  PIN = '158.0.8127.2'
  REV = '681cd621e82e606d7536eae8ea2576afb3c7d65c'

  def setUp(self):
    self.tmp = pathlib.Path(tempfile.mkdtemp())
    self.addCleanup(shutil.rmtree, self.tmp)
    shutil.copytree(ROOT / 'scripts', self.tmp / 'scripts',
                    ignore=shutil.ignore_patterns('__pycache__', 'tests'))
    shutil.copytree(ROOT / 'build', self.tmp / 'build')
    (self.tmp / 'patches').mkdir()
    (self.tmp / 'branding').mkdir()
    (self.tmp / 'CHROMIUM_VERSION').write_text(f'{self.PIN}\n{self.REV}\n')

  def plan(self, *args, **env):
    full = {k: v for k, v in os.environ.items()
            if not k.startswith('KOAN_') and k != 'CI'}
    full.update(GOOD_MAC, **env)
    r = subprocess.run(['bash', str(self.tmp / 'scripts' / 'build.sh'),
                        '--dry-run', *args],
                       capture_output=True, text=True, env=full, check=False)
    self.assertEqual(r.returncode, 0, r.stderr)
    return r.stdout

  def fake_src(self, commit_msg='pristine'):
    """A chromium/src git repo; returns its HEAD."""
    src = self.tmp / 'chromium' / 'src'
    src.mkdir(parents=True, exist_ok=True)
    git = ['git', '-C', str(src), '-c', 'user.name=t', '-c', 'user.email=t@t']
    subprocess.run(git[:3] + ['init', '-q'], check=True)
    subprocess.run(git + ['commit', '-q', '--allow-empty', '-m', commit_msg],
                   check=True)
    return subprocess.run(git[:3] + ['rev-parse', 'HEAD'], check=True,
                          capture_output=True, text=True).stdout.strip()

  def stamp(self, rev):
    state = self.tmp / '.cache' / 'build-state'
    state.mkdir(parents=True, exist_ok=True)
    (state / 'synced').write_text(rev + '\n')

  def test_fresh_clone_fetches_everything_and_installs(self):
    out = self.plan()
    self.assertIn('fetch        would: fetch depot_tools and Chromium', out)
    self.assertIn('koan         would: apply', out)
    self.assertIn('autoninja -C out/Koan -j 3 chrome', out)
    self.assertIn('install      would', out)
    self.assertNotIn('FAIL', out)

  def test_interrupted_fetch_resumes(self):
    self.fake_src()  # some other HEAD, no finished sync
    self.assertIn('fetch        would: resume the fetch', self.plan())

  def test_synced_checkout_is_left_alone(self):
    head = self.fake_src()
    (self.tmp / 'CHROMIUM_VERSION').write_text(f'{self.PIN}\n{head}\n')
    self.stamp(head)
    self.assertIn('fetch        skip: Chromium', self.plan())

  def test_pin_moved_by_git_pull_updates(self):
    head = self.fake_src()
    self.stamp(head)  # a finished sync of the old pin; CHROMIUM_VERSION moved
    self.assertIn('fetch        would: move the checkout to 158.0.8127.2',
                  self.plan())

  def test_fast_and_no_install(self):
    out = self.plan('--fast', '--no-install', JOBS='10')
    self.assertIn('10 (--fast)', out)
    self.assertIn('install      skip', out)

  def test_signs_before_install_unless_koan_sign_is_0(self):
    out = self.plan()
    self.assertIn('sign         would: scripts/mac_sign.sh', out)
    self.assertLess(out.index('sign         would'),
                    out.index('install      would'))
    self.assertIn('sign         skip', self.plan(KOAN_SIGN='0'))

  def test_maintainer_folder_does_not_install(self):
    out = self.plan(KOAN_MAC='/Users/<user>/work/koan-mac')
    self.assertIn('install      skip', out)

  def test_missing_prereqs_are_reported(self):
    out = self.plan(KOAN_FAKE_METAL='', KOAN_FAKE_DISK_GB='20')
    self.assertIn('FAIL  Metal toolchain', out)
    self.assertIn('FAIL  Disk: 20 GB', out)


if __name__ == '__main__':
  unittest.main()
