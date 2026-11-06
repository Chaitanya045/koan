#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/ci/check_private_info.py (private hosts, IPs, key paths).

This file is scanned like any other, so what the checker must catch is built at
runtime from pieces; the only literals are allowlisted fake tokens.
"""

import pathlib
import sys
import unittest

sys.path.insert(0,
                str(pathlib.Path(__file__).resolve().parent.parent / 'ci'))
import check_private_info as c  # noqa: E402


def ip(*parts):
  return '.'.join(str(p) for p in parts)


def kinds(text, tokens=frozenset()):
  return [(kind, match) for _, kind, match in c.scan_text(text, tokens)]


class ScanTest(unittest.TestCase):

  def test_finds_a_macbook_host_name_ip_and_key_path(self):
    host = 'lab-' + 'macbook-air'
    key = 'lab_key_' + 'ed25519'
    self.assertEqual(sorted(kinds('ssh -i ~/.ssh/%s me@%s' % (key, host))),
                     sorted([('ssh key path', '.ssh/' + key),
                             ('macbook host name', host)]))
    addr = ip(192, 168, 7, 9)
    self.assertEqual(kinds('no route to ' + addr),
                     [('private IP address', addr)])

  def test_finds_local_host_names_but_not_other_dot_local(self):
    host = 'lab-mac.' + 'local'
    self.assertEqual(kinds('ssh ' + host), [('.local host name', host)])
    self.assertEqual(kinds('chrome.storage.local.set and ~/.local/share/x'), [])
    self.assertEqual(kinds('localhost, 127.0.0.1, 8.8.8.8, 158.0.8127.2'), [])

  def test_private_ranges_only(self):
    for a in (ip(10, 1, 2, 3), ip(172, 16, 4, 1), ip(172, 31, 255, 1),
              ip(192, 168, 1, 20)):
      self.assertEqual(kinds('at ' + a), [('private IP address', a)], a)
    for a in (ip(172, 15, 0, 1), ip(172, 32, 0, 1), ip(11, 0, 0, 1),
              ip(192, 169, 0, 1), ip(110, 1, 2, 3)):
      self.assertEqual(kinds('at ' + a), [], a)

  def test_key_file_names_and_ssh_paths(self):
    name = 'id_' + 'rsa'
    self.assertEqual(kinds(name), [('ssh key path', name)])
    path = '.ssh/' + 'config'
    self.assertEqual(kinds('~/' + path), [('ssh key path', path)])

  def test_a_mac_user_folder_with_a_real_name_is_found(self):
    path = '/Users/' + 'alice'
    self.assertEqual(kinds('cd %s/work' % path), [('Mac user folder', path)])
    for ok in ('/Users/<user>/work', '/Users/${USER}/x', '/Users/$HOME/x',
               '/usr/local/bin'):
      self.assertEqual(kinds(ok), [], ok)

  def test_placeholders_pass(self):
    self.assertEqual(kinds('ssh -i ~/.ssh/<ssh-key> <user>@<mac-host> (<mac-ip>)'), [])


class AllowlistTest(unittest.TestCase):

  def test_exact_tokens_only(self):
    tokens = c.allowed_tokens('\n'.join(['# note', 'example-host.local', '10.0.0.1', '']))
    self.assertEqual(tokens, {'example-host.local', '10.0.0.1'})
    self.assertEqual(kinds('example-host.local at 10.0.0.1', tokens), [])
    # Another host or address on the same line is still found: the allowance
    # is for the token, not for the line.
    other_host, other_ip = 'other-host.' + 'local', ip(10, 0, 0, 2)
    self.assertEqual(kinds('%s at %s and 10.0.0.1' % (other_host, other_ip), tokens),
                     [('.local host name', other_host),
                      ('private IP address', other_ip)])

  def test_the_shipped_allowlist_is_fake_tokens_not_files(self):
    tokens = c.allowed_tokens((c.ROOT / c.ALLOWLIST).read_text())
    self.assertIn('example-host.local', tokens)
    for token in tokens:
      self.assertNotIn('*', token)
      self.assertFalse(token.endswith(('.py', '.md', '.sh', '.mjs')), token)


class RepositoryTest(unittest.TestCase):

  def test_check_runs_over_all_files(self):
    files = [('a.md', 'at ' + ip(10, 0, 0, 5)), ('docs/x.md', 'fine')]
    self.assertEqual([p[0] for p in c.check(files, frozenset())], ['a.md'])

  def test_the_repository_is_clean(self):
    tokens = c.allowed_tokens((c.ROOT / c.ALLOWLIST).read_text())
    self.assertEqual(c.check(c.tracked_text_files(), tokens), [])


if __name__ == '__main__':
  unittest.main()
