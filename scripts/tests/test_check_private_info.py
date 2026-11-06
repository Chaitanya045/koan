#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/ci/check_private_info.py (private hosts, IPs, key paths)."""

import pathlib
import sys
import unittest

sys.path.insert(0,
                str(pathlib.Path(__file__).resolve().parent.parent / 'ci'))
import check_private_info as c  # noqa: E402


def kinds(text):
  return [(kind, match) for _, kind, match in c.scan_text('f', text)]


class ScanTest(unittest.TestCase):

  def test_finds_the_original_leaks(self):
    self.assertEqual(kinds('ssh -i ~/.ssh/koan_mac_' + 'ed25519 me@some-macbook-air'),
                     [('macbook host name', 'some-macbook-air'),
                      ('ssh key path', '.ssh/koan_mac_ed25519')])
    self.assertEqual(kinds('no route to ' + '192.168.' + '0.104'),
                     [('private IP address', '192.168.0.104')])

  def test_finds_local_host_names_but_not_other_dot_local(self):
    self.assertEqual(kinds('ssh my-mac.' + 'local'), [('.local host name', 'my-mac.local')])
    self.assertEqual(kinds('chrome.storage.local.set and ~/.local/share/x'), [])
    self.assertEqual(kinds('localhost, 127.0.0.1, 8.8.8.8, 158.0.8127.2'), [])

  def test_private_ranges_only(self):
    for ip in ('10.0.0.5', '172.16.4.1', '172.31.255.1', '192.168.1.20'):
      self.assertEqual(kinds('at ' + ip), [('private IP address', ip)], ip)
    for ip in ('172.15.0.1', '172.32.0.1', '11.0.0.1', '192.169.0.1', '110.1.2.3'):
      self.assertEqual(kinds('at ' + ip), [], ip)

  def test_key_file_names(self):
    self.assertEqual(kinds('id_' + 'rsa'), [('ssh key path', 'id_rsa')])
    self.assertEqual(kinds('~/.ssh/' + 'config'), [('ssh key path', '.ssh/config')])

  def test_placeholders_and_marked_lines_pass(self):
    self.assertEqual(kinds('ssh -i ~/.ssh/<ssh-key> <user>@<mac-host> (<mac-ip>)'), [])
    self.assertEqual(kinds('192.168.' + '0.1  # koan-allow-private'), [])


class CheckTest(unittest.TestCase):

  def test_allowlisted_files_are_skipped(self):
    files = [('a.md', 'at 10.0.0.5'), ('docs/x.md', 'fine'), ('b/c.md', 'at 10.0.0.6')]
    self.assertEqual([p[0] for p in c.check(files, [])], ['a.md', 'b/c.md'])
    self.assertEqual([p[0] for p in c.check(files, ['b/*'])], ['a.md'])

  def test_the_repository_is_clean(self):
    globs = c.allowed_globs((c.ROOT / c.ALLOWLIST).read_text())
    self.assertEqual(c.check(c.tracked_text_files(), globs), [])


if __name__ == '__main__':
  unittest.main()
