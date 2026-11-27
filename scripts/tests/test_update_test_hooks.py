#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""What the update check trusts in what is published.

The app builds the release key in (src/browser/updates/public_key_header.py),
and only a test build has the switches that point the check at another
manifest and key (koan_update_test_hooks, src/browser/updates/BUILD.gn).
These tests check the key's header, and that the arg stays off in every
build that is published: no args file sets it, CI sets it only for a run
that is neither a release nor kept as a base for the nightlies, and those
runs check their binary before packaging it.
"""

import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
UPDATES = ROOT / 'src' / 'browser' / 'updates'
WORKFLOWS = ROOT / '.github' / 'workflows'
sys.path.insert(0, str(UPDATES))
sys.path.insert(0, str(ROOT / 'scripts' / 'ci'))
import public_key_header  # noqa: E402
import update_manifest  # noqa: E402

ARG = 'koan_update_test_hooks'
HOOKS_ON = "(!inputs.keep_base && !inputs.release) && '1' || '0'"
PUBLISHED = 'inputs.release || inputs.keep_base'


def _step(workflow, name):
  """The text of the step called `name` in a workflow, up to the next one."""
  text = (WORKFLOWS / workflow).read_text()
  start = text.index('      - name: %s\n' % name)
  end = text.find('\n      - name: ', start + 1)
  return text[start:end if end >= 0 else len(text)]


class PublicKeyHeaderTest(unittest.TestCase):

  def test_builds_the_release_key_in(self):
    key_file = UPDATES / 'koan_update_public_key.txt'
    raw = public_key_header.read_key(key_file.read_text())
    self.assertEqual(raw, update_manifest.read_public_key(key_file))
    header = public_key_header.header(raw)
    self.assertIn('kBuiltInPublicKey[32]', header)
    bytes_in_header = [
        int(b, 16) for b in re.findall(r'0x([0-9a-f]{2}),', header)
    ]
    self.assertEqual(bytes(bytes_in_header), raw)

  def test_fails_on_anything_but_one_key(self):
    for text in ('', 'not base64!', 'AAAA', 'A' * 86 + '==',
                 '9L/DeJJ6+uRRzg8UVhYmAO1G9E3OX977m+ZvJBtu3dY'):
      with self.assertRaises(ValueError, msg=text):
        public_key_header.read_key(text)
    with tempfile.TemporaryDirectory() as tmp:
      bad = pathlib.Path(tmp, 'key.txt')
      bad.write_text('AAAA\n')
      out = pathlib.Path(tmp, 'key.h')
      result = subprocess.run([
          sys.executable, '-I',
          str(UPDATES / 'public_key_header.py'), '--input',
          str(bad), '--output',
          str(out)
      ],
                              capture_output=True,
                              text=True)
      self.assertNotEqual(result.returncode, 0)
      self.assertFalse(out.exists())


class TestHooksStayOffTest(unittest.TestCase):

  def test_off_by_default(self):
    gn = (UPDATES / 'BUILD.gn').read_text()
    self.assertRegex(gn, r'declare_args\(\) \{[^}]*\n  %s = false\n' % ARG)
    self.assertIn('assert(!%s || !is_official_build' % ARG, gn)

  def test_no_args_file_sets_it(self):
    for args in (ROOT / 'build').glob('*.gn'):
      self.assertNotIn(ARG, args.read_text(), args)

  def test_build_sh_adds_it_only_on_request(self):
    text = (ROOT / 'scripts' / 'build.sh').read_text()
    self.assertEqual(text.count(ARG), 1)
    self.assertIn(
        'if [[ ${KOAN_UPDATE_TEST_HOOKS:-0} == 1 ]]; then\n'
        '    echo "%s = true" >> "$new"' % ARG, text)

  def test_ci_turns_it_on_only_for_runs_that_are_not_published(self):
    for chain in ('linux-chain.yml', 'macos-chain.yml'):
      step = _step(chain, 'Fetch Chromium, apply patches and branding, gn gen')
      self.assertIn('KOAN_CI_UPDATE_TEST_HOOKS: ${{ %s }}' % HOOKS_ON, step,
                    chain)
      self.assertEqual(
          (WORKFLOWS / chain).read_text().count('KOAN_CI_UPDATE_TEST_HOOKS'),
          1, chain)

  def test_published_builds_check_their_binary(self):
    for workflow, package in (('linux-build.yml', 'Make the Flatpak'),
                              ('macos-build.yml', 'Make the disk image')):
      text = (WORKFLOWS / workflow).read_text()
      check = _step(workflow, 'Check that the update test hooks are not '
                    'built in')
      self.assertIn('(%s)' % PUBLISHED, check, workflow)
      self.assertIn('scripts/ci/check_update_test_hooks.sh', check, workflow)
      self.assertLess(text.index(check), text.index('- name: %s\n' % package),
                      workflow)

  def test_releases_and_nightlies_are_the_published_runs(self):
    # release.yml builds as a release; the nightlies publish only the runs
    # they keep as a base.
    release = (WORKFLOWS / 'release.yml').read_text()
    calls = list(
        re.finditer(r'uses: \./\.github/workflows/(\w+)-chain\.yml'
                    r'(?:\n(?!  \S).*)*', release))
    self.assertTrue(calls)
    for call in calls:
      self.assertIn('release: true', call.group(0))
    for workflow, output in (('linux.yml', 'trusted'), ('macos.yml',
                                                         'develop')):
      text = (WORKFLOWS / workflow).read_text()
      self.assertIn(
          "keep_base: ${{ needs.plan.outputs.%s == 'true' }}" % output, text)
      publish = text[text.index('\n  release:\n'):]
      self.assertIn("needs.plan.outputs.%s == 'true'" % output,
                    publish.split('\n    steps:')[0], workflow)

  def _source_env(self, script, env, tmp):
    """KOAN_UPDATE_TEST_HOOKS after sourcing a copy of `script` in `tmp`."""
    ci = pathlib.Path(tmp, 'scripts', 'ci')
    ci.mkdir(parents=True, exist_ok=True)
    shutil.copy(ROOT / 'scripts' / 'ci' / script, ci / script)
    clean = {k: v for k, v in os.environ.items() if not k.startswith('KOAN_')}
    clean.update(env)
    return subprocess.run(
        ['bash', '-c', 'source "$1" >/dev/null; echo "$KOAN_UPDATE_TEST_HOOKS"',
         'bash', str(ci / script)],
        env=clean, capture_output=True, text=True, check=True).stdout.strip()

  def test_the_fetch_job_hands_it_to_the_build_jobs(self):
    for script in ('linux_env.sh', 'mac_env.sh'):
      with tempfile.TemporaryDirectory() as tmp:
        # Nothing said: off.
        self.assertEqual(self._source_env(script, {}, tmp), '0', script)
        # The fetch job of an unpublished run, then a build job.
        self.assertEqual(
            self._source_env(script, {'KOAN_CI_UPDATE_TEST_HOOKS': '1'}, tmp),
            '1', script)
        self.assertEqual(self._source_env(script, {}, tmp), '1', script)
        # A published run's fetch job, also on a tree that said 1.
        self.assertEqual(
            self._source_env(script, {'KOAN_CI_UPDATE_TEST_HOOKS': '0'}, tmp),
            '0', script)
        self.assertEqual(self._source_env(script, {}, tmp), '0', script)


class CheckScriptTest(unittest.TestCase):

  def _check(self, args_gn, binary):
    with tempfile.TemporaryDirectory() as tmp:
      out = pathlib.Path(tmp)
      (out / 'args.gn').write_text(args_gn)
      (out / 'chrome').write_bytes(binary)
      return subprocess.run([
          str(ROOT / 'scripts' / 'ci' / 'check_update_test_hooks.sh'),
          str(out),
          str(out / 'chrome')
      ],
                            capture_output=True).returncode

  def test_passes_a_build_without_hooks(self):
    self.assertEqual(self._check('is_debug = false\n', b'\0koan\0'), 0)

  def test_fails_a_build_with_hooks(self):
    self.assertNotEqual(
        self._check('is_debug = false\n%s = true\n' % ARG, b'\0koan\0'), 0)
    self.assertNotEqual(
        self._check('is_debug = false\n', b'\0koan-update-manifest-url\0'), 0)

  def test_the_switch_name_is_the_one_the_hooks_use(self):
    source = (UPDATES / 'koan_update_source.h').read_text()
    self.assertIn('kManifestUrlSwitch[] = "koan-update-manifest-url"', source)


if __name__ == '__main__':
  unittest.main()
