#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Koan's Flatpak may read exactly the folders its import looks in (#493).

The Linux browser table in src/browser/importer/koan_import_sources.cc and
the read-only --filesystem entries in flatpak/dev.koan.Koan.yml must name
the same folders: a folder only in the table is never found in the Flatpak,
one only in the manifest is access Koan doesn't use.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SOURCES = ROOT / 'src/browser/importer/koan_import_sources.cc'
MANIFEST = ROOT / 'flatpak/dev.koan.Koan.yml'

# The Flatpak apps' icons (koan_import_icons_linux.cc), not a browser's data.
ICON_ARGS = {
    '--filesystem=xdg-data/flatpak/exports/share/icons:ro',
    '--filesystem=/var/lib/flatpak/exports/share/icons:ro',
}


def linux_table_dirs(source):
  """The data folders of the Linux browser table, in order."""
  start = source.index('// Linux (#493)')
  table = source[source.index('constexpr KnownBrowser kBrowsers[] = {',
                              start):]
  table = table[:table.index('};')]
  return re.findall(r'(?:Gecko|ChromiumBased)\(\s*u"[^"]+",\s*"([^"]+)"',
                    table)


def manifest_home_args(manifest):
  """The manifest's --filesystem entries under the home folder."""
  return re.findall(r'^\s*-\s*(--filesystem=~/\S+)\s*$', manifest,
                    re.MULTILINE)


class FlatpakImportPathsTest(unittest.TestCase):

  def setUp(self):
    self.dirs = linux_table_dirs(SOURCES.read_text())
    self.manifest = MANIFEST.read_text()

  def test_table_is_parsed(self):
    self.assertIn('.zen', self.dirs)
    self.assertIn('.config/google-chrome', self.dirs)
    self.assertIn('.var/app/com.google.Chrome/config/google-chrome',
                  self.dirs)

  def test_manifest_reads_exactly_the_table_folders(self):
    expected = [f'--filesystem=~/{d}:ro' for d in self.dirs]
    self.assertEqual(sorted(expected),
                     sorted(manifest_home_args(self.manifest)))

  def test_every_import_folder_is_read_only(self):
    for arg in manifest_home_args(self.manifest):
      self.assertTrue(arg.endswith(':ro'), arg)

  def test_icons_are_read_only(self):
    args = set(
        re.findall(r'^\s*-\s*(--filesystem=\S+)\s*$', self.manifest,
                   re.MULTILINE))
    self.assertTrue(ICON_ARGS <= args)

  def test_no_whole_folders(self):
    # Never the home folder, ~/.config or ~/.var/app as a whole.
    for d in self.dirs:
      self.assertNotIn(d.rstrip('/'), {'', '.config', '.var/app', '.var'})
      self.assertFalse(d.startswith('/'), d)
      self.assertNotIn('..', d.split('/'), d)


if __name__ == '__main__':
  unittest.main()
