#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Koan's Flatpak may read exactly the folders its import looks in (#493),
and the agent may upload from none of them (#514).

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
# The agent's upload refuses the folders RefusedHomeFolders() lists (#514).
UPLOAD_LOGIC = ROOT / 'src/browser/agent/koan_agent_upload_logic.cc'

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


def refused_home_folders(source):
  """The folders RefusedHomeFolders() lists (all platforms' and Linux's), or
  None if the file has no such function."""
  start = source.find('std::vector<base::FilePath> RefusedHomeFolders(')
  if start < 0:
    return None
  body = source[start:source.index('\n}\n', start)]
  return re.findall(r'"([^"]+)"', body)


def outside_refused(dirs, refused):
  """The import folders under none of the refused folders."""
  def under(d, r):
    return d == r or d.startswith(r.rstrip('/') + '/')
  return [d for d in dirs if not any(under(d, r) for r in refused)]


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


class ImportFoldersRefusedForUploadsTest(unittest.TestCase):
  """Every folder Koan imports from (other browsers' profiles: cookies,
  saved passwords) is one the agent may never upload from (#514)."""

  def test_import_table_inside_refused_folders(self):
    if not UPLOAD_LOGIC.exists():
      self.skipTest('no koan_agent_upload_logic.cc yet (#514)')
    refused = refused_home_folders(UPLOAD_LOGIC.read_text())
    if refused is None:
      self.skipTest('no RefusedHomeFolders() yet (#514)')
    self.assertIn('.var/app', refused)
    dirs = linux_table_dirs(SOURCES.read_text())
    self.assertTrue(dirs)
    self.assertEqual([], outside_refused(dirs, refused))

  def test_outside_refused(self):
    self.assertEqual(
        ['.config/new-browser'],
        outside_refused(['.zen', '.mozilla/firefox', '.config/new-browser',
                         '.var/app/com.google.Chrome/config/google-chrome'],
                        ['.zen', '.mozilla', '.var/app']))
    # A prefix of a name isn't its folder.
    self.assertEqual(['.zenith'], outside_refused(['.zenith'], ['.zen']))


if __name__ == '__main__':
  unittest.main()
