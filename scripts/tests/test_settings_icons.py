#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Every Settings pane has an icon in the Settings sidebar.

koan_settings.js gives each pane's sidebar item the class `icon-<id>`, and
koan_settings.css draws it with a mask. Without a rule the item showed a
blank square (Agent Control, 2026-11-11). This parses the C++ and CSS, so it
runs in CI without a build.
"""

import pathlib
import re
import unittest

WEBUI = pathlib.Path(__file__).resolve().parents[2] / 'src/browser/ui/webui'


def pane_ids():
  rows = (WEBUI / 'koan_settings_rows.cc').read_text()
  header = (WEBUI / 'koan_settings_rows.h').read_text()
  table = re.search(r'kPanes\[\]\s*=\s*\{(.*?)\n\};', rows, re.S)
  assert table, 'kPanes not found'
  constants = dict(
      re.findall(r'constexpr char (k\w+)\[\]\s*=\s*"([^"]+)"', header))
  ids = []
  for entry in re.findall(r'\{\s*("[^"]+"|k\w+)\s*,', table.group(1)):
    ids.append(entry.strip('"') if entry.startswith('"') else constants[entry])
  return ids


class SettingsIconsTest(unittest.TestCase):

  def test_every_pane_has_an_icon(self):
    css = (WEBUI / 'resources/koan_settings.css').read_text()
    ids = pane_ids()
    self.assertIn('agents', ids)
    for pane in ids:
      with self.subTest(pane=pane):
        self.assertTrue(
            re.search(r'\.icon-%s\s*\{[^}]*mask-image' % re.escape(pane), css),
            'koan_settings.css has no .icon-%s rule' % pane)


if __name__ == '__main__':
  unittest.main()
