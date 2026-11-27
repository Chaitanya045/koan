#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/ci/linux_apt_sources.sh, on a fake /etc/apt.

The fake holds what the GitHub ubuntu-24.04 image ships (Ubuntu's archive
in deb822 ubuntu.sources, a third-party microsoft-prod.list) plus a dead
source, and checks that only Ubuntu's files reach the directory apt-get is
pointed at, and that no Ubuntu source at all is an error. Skipped when bash
or awk is missing.
"""

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts' / 'ci' / 'linux_apt_sources.sh'

UBUNTU_SOURCES = """\
Types: deb
URIs: http://azure.archive.ubuntu.com/ubuntu/
Suites: noble noble-updates noble-backports
Components: main universe restricted multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg

Types: deb
URIs: http://security.ubuntu.com/ubuntu
Suites: noble-security
Components: main universe restricted multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
"""
MICROSOFT_LIST = (
    'deb [arch=amd64,armhf,arm64 signed-by=/usr/share/keyrings/microsoft.gpg]'
    ' https://packages.microsoft.com/ubuntu/24.04/prod noble main\n')


@unittest.skipUnless(shutil.which('bash') and shutil.which('awk'),
                     'needs bash and awk')
class AptSourcesTest(unittest.TestCase):

  def setUp(self):
    tmp = tempfile.TemporaryDirectory()
    self.addCleanup(tmp.cleanup)
    self.tmp = pathlib.Path(tmp.name)
    self.apt = self.tmp / 'apt'
    self.parts = self.apt / 'sources.list.d'
    self.parts.mkdir(parents=True)
    self.out = self.tmp / 'out'

  def write(self, name, text):
    path = self.apt / name
    path.write_text(text)
    return path

  def run_script(self):
    return subprocess.run(
        ['bash', str(SCRIPT), str(self.apt), str(self.out)],
        capture_output=True, text=True,
        env={'PATH': os.environ['PATH']})

  def kept(self):
    return sorted(p.name for p in self.out.iterdir())

  def test_runner_image_keeps_only_ubuntu(self):
    self.write('sources.list', '# Ubuntu sources have moved to '
               '/etc/apt/sources.list.d/ubuntu.sources\n')
    self.write('sources.list.d/ubuntu.sources', UBUNTU_SOURCES)
    self.write('sources.list.d/microsoft-prod.list', MICROSOFT_LIST)
    # A dead third-party source, as packages.microsoft.com was in run
    # 45980089943.
    self.write('sources.list.d/dead.list',
               'deb http://dead.invalid/apt noble main\n')
    r = self.run_script()
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertEqual(self.kept(), ['ubuntu.sources'])
    self.assertEqual((self.out / 'ubuntu.sources').read_text(),
                     UBUNTU_SOURCES)
    self.assertIn('microsoft-prod.list', r.stderr)
    self.assertIn('dead.list', r.stderr)

  def test_mirror_list(self):
    # The runner image's ubuntu.sources (run 45983942868).
    sources = UBUNTU_SOURCES.replace(
        'http://azure.archive.ubuntu.com/ubuntu/',
        'mirror+file:/etc/apt/apt-mirrors.txt')
    self.write('sources.list.d/ubuntu.sources', sources)
    self.write('sources.list.d/microsoft-prod.list', MICROSOFT_LIST)
    self.write('apt-mirrors.txt',
               'http://azure.archive.ubuntu.com/ubuntu/\tpriority:1\n'
               'https://archive.ubuntu.com/ubuntu/\tpriority:2\n'
               'https://security.ubuntu.com/ubuntu/\tpriority:3\n')
    r = self.run_script()
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertEqual(self.kept(), ['ubuntu.sources'])

  def test_mirror_list_with_other_host_is_skipped(self):
    self.write('sources.list.d/ubuntu.sources', UBUNTU_SOURCES)
    self.write('sources.list.d/m.sources',
               'Types: deb\nURIs: mirror+file:/etc/apt/m.txt\n'
               'Suites: noble\nComponents: main\n')
    self.write('m.txt', 'http://archive.ubuntu.com/ubuntu/\n'
               'http://mirror.example.com/ubuntu/\n')
    r = self.run_script()
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertEqual(self.kept(), ['ubuntu.sources'])

  def test_one_line_sources_list(self):
    self.write('sources.list',
               '# comment\n'
               'deb http://archive.ubuntu.com/ubuntu noble main\n'
               'deb-src [arch=amd64] http://ports.ubuntu.com/ubuntu-ports '
               'noble main\n')
    r = self.run_script()
    self.assertEqual(r.returncode, 0, r.stderr)
    # apt reads only *.list and *.sources in the parts directory.
    self.assertEqual(self.kept(), ['sources.list.list'])

  def test_mixed_file_is_skipped(self):
    self.write('sources.list.d/ubuntu.sources', UBUNTU_SOURCES)
    self.write('sources.list.d/mixed.list',
               'deb http://archive.ubuntu.com/ubuntu noble main\n'
               'deb https://evilubuntu.com/apt noble main\n')
    r = self.run_script()
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertEqual(self.kept(), ['ubuntu.sources'])

  def test_disabled_stanza_does_not_count(self):
    self.write('sources.list.d/ubuntu.sources', UBUNTU_SOURCES)
    self.write('sources.list.d/other.sources',
               'Types: deb\nURIs: http://archive.ubuntu.com/ubuntu\n'
               'Suites: noble\nComponents: main\n\n'
               'Enabled: no\nTypes: deb\nURIs: https://example.com/apt\n'
               'Suites: noble\nComponents: main\n')
    r = self.run_script()
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertEqual(self.kept(), ['other.sources', 'ubuntu.sources'])

  def test_no_ubuntu_source_fails(self):
    self.write('sources.list.d/microsoft-prod.list', MICROSOFT_LIST)
    r = self.run_script()
    self.assertNotEqual(r.returncode, 0)
    self.assertIn("no apt source for Ubuntu's archive", r.stderr)

  def test_out_dir_is_emptied(self):
    self.write('sources.list.d/ubuntu.sources', UBUNTU_SOURCES)
    self.out.mkdir()
    (self.out / 'stale.list').write_text(MICROSOFT_LIST)
    r = self.run_script()
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertEqual(self.kept(), ['ubuntu.sources'])


if __name__ == '__main__':
  unittest.main()
