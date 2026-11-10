#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for src/browser/updates/lastchange.py (the build's commit stamp)."""

import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src' / 'browser' / 'updates'))
import lastchange  # noqa: E402


class GitHubRepoTest(unittest.TestCase):

  def test_https_and_ssh_remotes(self):
    for url in ('https://github.com/Chaitanya045/koan.git',
                'https://github.com/Chaitanya045/koan',
                'https://token@github.com/Chaitanya045/koan.git',
                'git@github.com:Chaitanya045/koan.git',
                'ssh://git@github.com/Chaitanya045/koan.git\n'):
      self.assertEqual(lastchange.github_repo(url), 'Chaitanya045/koan', url)

  def test_other_remotes_have_none(self):
    for url in ('', 'https://gitlab.com/a/koan.git', '/home/me/koan',
                'https://github.com/only-owner'):
      self.assertEqual(lastchange.github_repo(url), '', url)


class StampTest(unittest.TestCase):

  def test_stamps_this_clone(self):
    commit, commit_time, _, head_log = lastchange.read_stamp(str(ROOT))
    if not commit:
      self.skipTest('not a git clone')
    self.assertRegex(commit, r'^[0-9a-f]{40}$')
    self.assertGreater(commit_time, 0)
    self.assertTrue(head_log)

  def test_outside_a_clone_is_empty(self):
    with tempfile.TemporaryDirectory() as empty:
      self.assertEqual(lastchange.read_stamp(empty), ('', 0, '', ''))

  def test_writes_header_and_depfile(self):
    with tempfile.TemporaryDirectory() as out:
      header = pathlib.Path(out, 'koan_lastchange.h')
      depfile = pathlib.Path(out, 'koan_lastchange.d')
      subprocess.run([
          sys.executable, '-I',
          str(ROOT / 'src/browser/updates/lastchange.py'), '--output',
          str(header), '--depfile',
          str(depfile)
      ],
                     check=True)
      text = header.read_text()
      self.assertIn('#define KOAN_BUILD_COMMIT "', text)
      self.assertIn('#define KOAN_BUILD_COMMIT_TIME ', text)
      self.assertTrue(depfile.read_text().startswith(str(header) + ':'))


if __name__ == '__main__':
  unittest.main()
