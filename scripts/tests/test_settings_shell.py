#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Koan's panes in chrome://settings agree across the C++, the JS and the
hook patches.

Koan's settings are a SettingsPlugin of Chromium's settings app
(koan_settings_ui.h). The panes are listed in C++ (kPanes), but Chromium's
routes, menu items and the script tag are static markup in patches, and the
plugin is plain JS served by the browser. A pane or file renamed on one side
only gives a menu item that opens nothing, a blank icon (Agent Control,
2026-11-11) or a page without Koan's panes, and no compile error. This parses
the files, so it runs in CI without a build.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
WEBUI = ROOT / 'src/browser/ui/webui'
SETTINGS = ROOT / 'patches/chrome/browser/resources/settings'


def added_lines(patch):
  return '\n'.join(
      line[1:] for line in (SETTINGS / patch).read_text().splitlines()
      if line.startswith('+') and not line.startswith('+++'))


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


JS = (WEBUI / 'resources/koan_settings.js').read_text()


class SettingsShellTest(unittest.TestCase):

  def test_routes_match_the_panes(self):
    route = added_lines('route.ts.patch')
    listed = re.search(r'for \(const pane of \[([^\]]*)\]\)', route)
    self.assertTrue(listed, 'route.ts patch: no Koan pane list')
    self.assertEqual(
        [p.strip().strip("'").lower() for p in listed.group(1).split(',')],
        pane_ids())

  def test_menu_lists_every_pane_with_its_title_and_icon(self):
    menu = added_lines('settings_menu/settings_menu.html.patch')
    icons = re.search(r'const ICONS = \{(.*?)\n\};', JS, re.S)
    self.assertTrue(icons, 'koan_settings.js: no ICONS')
    icon_ids = re.findall(r'^\s*(\w+):', icons.group(1), re.M)
    hrefs = re.findall(r'href="/koan/(\w+)"', menu)
    self.assertEqual(hrefs, pane_ids())
    for pane in pane_ids():
      with self.subTest(pane=pane):
        title = 'koanPane' + pane[0].upper() + pane[1:]
        self.assertIn('$i18n{%s}' % title, menu)
        self.assertIn('icon="koan:%s"' % pane, menu)
        self.assertIn(pane, icon_ids)

  def test_first_pane_is_where_settings_opens(self):
    constants = (ROOT / 'src/common/koan_url_constants.h').read_text()
    self.assertIn('kSettingsURL[] = "chrome://settings/koan/%s"' %
                  pane_ids()[0], constants)
    self.assertIn("getRouteForPath('/koan/%s')" % pane_ids()[0],
                  added_lines('route.ts.patch'))

  def test_page_loads_the_served_files(self):
    ui = (WEBUI / 'koan_settings_ui.cc').read_text()
    served = set(re.findall(r'\{"(koan_[\w.]+)", File::', ui))
    self.assertIn('src="koan_settings.js"', added_lines('settings.html.patch'))
    used = {'koan_settings.js'}
    used |= set(re.findall(r"href: '(koan_[\w.]+)'", JS))
    used |= set(re.findall(r'url\(/(koan_[\w.]+)\)', ui))
    self.assertEqual(used, served)
    self.assertIn('<koan-settings-page>',
                  added_lines('settings_main/settings_main.html.patch'))
    self.assertIn("customElements.define('koan-settings-page'", JS)

  def test_messages_are_koan_prefixed_and_handled(self):
    handlers = ''.join(
        (WEBUI / f).read_text()
        for f in ('koan_settings_handler.cc',
                  'koan_agent_containers_handler.cc'))
    registered = set(re.findall(r'RegisterMessageCallback\(\s*"(\w+)"', handlers))
    sent = set(re.findall(r"(?:sendWithPromise|chrome\.send)\('(\w+)'", JS))
    self.assertTrue(sent)
    self.assertLessEqual(sent, registered)
    fired = set(re.findall(r'FireWebUIListener\(\s*"([\w-]+)"', handlers))
    listened = set(re.findall(r"addWebUiListener\('([\w-]+)'", JS))
    self.assertEqual(listened, fired)
    # The page shares one WebUI with Chromium's settings handlers, and a
    # second handler for a name is ignored.
    for name in registered:
      self.assertTrue(name.startswith('koan'), name)
    for name in fired:
      self.assertTrue(name.startswith('koan-'), name)

  def test_script_has_no_i18n_placeholders(self):
    # chrome://settings replaces $i18n{...} in the JS it serves.
    self.assertNotIn('$i18n', JS)


if __name__ == '__main__':
  unittest.main()
