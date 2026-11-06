#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""The remote-debugging patch turns on Chromium's default-user-data-dir check.

Chrome refuses --remote-debugging-port/-pipe on the default profile, but only
in Google-branded builds. Koan's patch makes the non-branded check default to
on. This test pins what the patch is allowed to do (flip that one default and
nothing else) and that the docs say so. The behaviour is tested on the Mac by
scripts/mac/remote_debugging.sh.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
PATCH = (ROOT / 'patches/chrome/browser/devtools/'
         'remote_debugging_server.cc.patch')
KNOB = 'g_enable_default_user_data_dir_check_for_chromium_branding_for_testing'


class RemoteDebuggingPatchTest(unittest.TestCase):

  def setUp(self):
    self.lines = PATCH.read_text().splitlines()
    body = [l for l in self.lines if not l.startswith(('---', '+++'))]
    self.removed = [l[1:] for l in body if l.startswith('-')]
    self.added = [l[1:] for l in body if l.startswith('+')]

  def test_patch_is_for_the_one_file(self):
    self.assertEqual(
        self.lines[0],
        'diff --git a/chrome/browser/devtools/remote_debugging_server.cc '
        'b/chrome/browser/devtools/remote_debugging_server.cc')
    self.assertEqual(
        [l for l in self.lines if l.startswith('diff --git')], [self.lines[0]])

  def test_flips_the_default_from_false_to_true(self):
    self.assertEqual(self.removed, ['    false;'])
    self.assertIn('    true;', self.added)

  def test_adds_only_the_flip_and_comments(self):
    extra = [l for l in self.added if l != '    true;']
    self.assertTrue(extra)
    self.assertTrue(extra[0].startswith('// Koan'), extra[0])
    for l in extra:
      self.assertTrue(l.startswith('//'), l)

  def test_the_knob_is_the_declaration_the_value_belongs_to(self):
    text = '\n'.join(self.lines)
    self.assertRegex(text, re.escape(KNOB) + r' =\n[-+ ]*    (false|true);')

  def test_docs_mention_it(self):
    rebase = (ROOT / 'docs/REBASE.md').read_text()
    self.assertIn('remote_debugging_server.cc', rebase)
    self.assertIn(KNOB, rebase)
    self.assertIn('scripts/mac/remote_debugging.sh', rebase)
    security = (ROOT / 'SECURITY.md').read_text()
    self.assertIn('non-default data directory', security)
    self.assertIn('already running', security)
    self.assertIn('case-insensitive', security)
    self.assertNotIn('read its cookies', security)

  def test_mac_script_keeps_the_pipe_fds_open_and_the_real_home_out(self):
    script = (ROOT / 'scripts/mac/remote_debugging.sh').read_text()
    self.assertIn('3</dev/null 4>/dev/null', script)
    self.assertNotIn('3<&-', script)
    self.assertIn('HOME="$fake_home"', script)


if __name__ == '__main__':
  unittest.main()
