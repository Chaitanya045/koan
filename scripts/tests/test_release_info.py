#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for src/version/koan_version.py and scripts/ci/release_info.py."""

import pathlib
import plistlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src' / 'version'))
sys.path.insert(0, str(ROOT / 'scripts' / 'ci'))
import koan_version  # noqa: E402
import release_info  # noqa: E402

CHROMIUM = koan_version.parse_chromium('158.0.8127.18')


class VersionTest(unittest.TestCase):

  def test_parses_semver_with_and_without_a_label(self):
    v = koan_version.parse('0.0.2-alpha.1\n')
    self.assertEqual((v.major, v.minor, v.patch, v.stage, v.number),
                     (0, 0, 2, 'alpha', 1))
    self.assertEqual(koan_version.parse('1.2.3').stage, '')

  def test_rejects_what_it_cant_encode(self):
    for text in ('0.0.2-alpha', '0.0.2-alpha.0', '0.0.2-dev.1', 'v0.0.2',
                 '0.0', '01.0.0', '0.100.0', '0.0.100', '0.0.1-rc.100',
                 '0.0.2-Alpha.1', ''):
      with self.assertRaises(ValueError, msg=text):
        koan_version.parse(text)

  def test_short_and_label(self):
    p = koan_version.parse
    self.assertEqual(koan_version.short(p('0.0.2-alpha.1')), '0.0.2')
    self.assertEqual(koan_version.label(p('0.0.2-alpha.1')), 'Alpha')
    self.assertEqual(koan_version.label(p('0.0.2-alpha.2')), 'Alpha 2')
    self.assertEqual(koan_version.label(p('0.3.0-beta.1')), 'Beta')
    self.assertEqual(koan_version.label(p('1.0.0-rc.3')), 'RC 3')
    self.assertEqual(koan_version.label(p('1.0.0')), '')

  def test_counter_follows_semver_order(self):
    order = [
        '0.0.1-alpha.1', '0.0.2-alpha.1', '0.0.2-alpha.2', '0.0.2-beta.1',
        '0.0.2-rc.1', '0.0.2-rc.99', '0.0.2', '0.0.3-alpha.1', '0.0.99',
        '0.1.0-alpha.1', '0.99.99', '1.0.0-alpha.1', '1.0.0', '1.0.1', '2.0.0'
    ]
    counters = [koan_version.counter(koan_version.parse(t)) for t in order]
    self.assertEqual(counters, sorted(counters))
    self.assertEqual(len(set(counters)), len(counters))
    self.assertEqual(counters[1], 2101)

  def test_bundle_version(self):
    v = koan_version.parse('0.0.2-alpha.1')
    self.assertEqual(koan_version.bundle_version(v, CHROMIUM), '8127.18.2101')
    # Above 0.0.1-alpha.1's, which was Chromium's own 8127.18.
    self.assertGreater(koan_version.bundle_key('8127.18.2101'),
                       koan_version.bundle_key('8127.18'))

  def test_title(self):
    p = koan_version.parse
    self.assertEqual(koan_version.title(p('0.0.2-alpha.1'), CHROMIUM),
                     'Koan 0.0.2 (Alpha) · Chromium 158.0.8127.18')
    self.assertEqual(koan_version.title(p('1.0.0'), CHROMIUM),
                     'Koan 1.0.0 · Chromium 158.0.8127.18')

  def test_title_matches_the_hand_made_first_release(self):
    self.assertEqual(
        koan_version.title(koan_version.parse('0.0.1-alpha.1'), CHROMIUM),
        'Koan 0.0.1 (Alpha) · Chromium 158.0.8127.18')

  def test_repository_files(self):
    version = koan_version.read()
    pin = koan_version.read_pin(ROOT / 'CHROMIUM_VERSION')
    self.assertTrue(koan_version.title(version, pin).startswith('Koan '))

  def test_chromium_version_file(self):
    with tempfile.TemporaryDirectory() as tmp:
      path = pathlib.Path(tmp) / 'VERSION'
      path.write_text('MAJOR=158\nMINOR=0\nBUILD=8127\nPATCH=18\n')
      self.assertEqual(koan_version.read_chromium_version_file(path), CHROMIUM)
      path.write_text('MAJOR=158\nMINOR=0\n')
      with self.assertRaises(ValueError):
        koan_version.read_chromium_version_file(path)

  def test_header(self):
    text = koan_version.header(koan_version.parse('0.0.2-alpha.1'))
    self.assertIn('#define KOAN_VERSION_FULL "0.0.2-alpha.1"\n', text)
    self.assertIn('#define KOAN_VERSION_SHORT "0.0.2"\n', text)
    self.assertIn('#define KOAN_VERSION_LABEL "Alpha"\n', text)

  def run_script(self, *args):
    return subprocess.run(
        [sys.executable, '-I', str(ROOT / 'src' / 'version' /
                                   'koan_version.py'), *args],
        capture_output=True, text=True)

  def test_plist_keeps_everything_else(self):
    with tempfile.TemporaryDirectory() as tmp:
      tmp = pathlib.Path(tmp)
      (tmp / 'VERSION').write_text('0.0.2-alpha.1\n')
      (tmp / 'chrome_VERSION').write_text(
          'MAJOR=158\nMINOR=0\nBUILD=8127\nPATCH=18\n')
      with open(tmp / 'in.plist', 'wb') as f:
        plistlib.dump({
            'CFBundleIdentifier': '${CHROMIUM_BUNDLE_ID}',
            'CFBundleShortVersionString': '158.0.8127.18',
            'CFBundleVersion': '8127.18',
        }, f)
      result = self.run_script(
          '--version-file', str(tmp / 'VERSION'), 'plist', '--input',
          str(tmp / 'in.plist'), '--output', str(tmp / 'out.plist'),
          '--depfile', str(tmp / 'out.d'), '--chromium-version-file',
          str(tmp / 'chrome_VERSION'))
      self.assertEqual(result.returncode, 0, result.stderr)
      with open(tmp / 'out.plist', 'rb') as f:
        plist = plistlib.load(f)
      self.assertEqual(plist, {
          'CFBundleIdentifier': '${CHROMIUM_BUNDLE_ID}',
          'CFBundleShortVersionString': '0.0.2',
          'CFBundleVersion': '8127.18.2101',
          'KoanVersion': '0.0.2-alpha.1',
      })
      self.assertIn(str(tmp / 'VERSION'), (tmp / 'out.d').read_text())

  def test_bad_version_file_fails_the_build(self):
    with tempfile.TemporaryDirectory() as tmp:
      tmp = pathlib.Path(tmp)
      (tmp / 'VERSION').write_text('0.0.2-preview\n')
      result = self.run_script('--version-file', str(tmp / 'VERSION'),
                               'header', '--output', str(tmp / 'h'),
                               '--depfile', str(tmp / 'd'))
      self.assertEqual(result.returncode, 1)
      self.assertIn('not a Koan version', result.stderr)


class ReleaseInfoTest(unittest.TestCase):

  def test_tag_must_match_version(self):
    v = koan_version.parse('0.0.2-alpha.1')
    release_info.check_tag('v0.0.2-alpha.1', v)
    for tag in ('v0.0.2-alpha.2', '0.0.2-alpha.1', 'v0.0.2', 'nightly'):
      with self.assertRaises(release_info.ReleaseError, msg=tag):
        release_info.check_tag(tag, v)

  def test_notes_must_exist_and_be_finished(self):
    with tempfile.TemporaryDirectory() as tmp:
      path = pathlib.Path(tmp) / 'n.md'
      with self.assertRaisesRegex(release_info.ReleaseError, 'no release'):
        release_info.check_notes(path)
      path.write_text('  \n')
      with self.assertRaisesRegex(release_info.ReleaseError, 'empty'):
        release_info.check_notes(path)
      path.write_text('# Notes\n<!-- TODO(release): fill in -->\n')
      with self.assertRaisesRegex(release_info.ReleaseError, 'line'):
        release_info.check_notes(path)
      path.write_text('# Notes\nDone.\n')
      release_info.check_notes(path)

  def test_the_published_notes_are_finished(self):
    release_info.check_notes(ROOT / 'docs' / 'releases' / '0.0.1-alpha.1.md')

  def test_previous_tag(self):
    tags = ['nightly', 'v0.0.1-alpha.1', 'v0.0.2-alpha.1', 'v0.0.2-beta.1',
            'v0.0.3-alpha.1', 'vlatest', 'v1.0']
    p = koan_version.parse
    self.assertEqual(release_info.previous_tag(tags, p('0.0.2-beta.1')),
                     'v0.0.2-alpha.1')
    self.assertEqual(release_info.previous_tag(tags, p('0.0.2-alpha.1')),
                     'v0.0.1-alpha.1')
    self.assertEqual(release_info.previous_tag(tags, p('0.0.1-alpha.1')), '')
    self.assertEqual(release_info.previous_tag(tags, p('0.1.0')),
                     'v0.0.3-alpha.1')

  def test_is_latest(self):
    p = koan_version.parse
    tags = ['v0.0.1-alpha.1', 'v0.0.2-alpha.1', 'nightly', 'vbogus']
    self.assertTrue(release_info.is_latest(tags, p('0.0.3-alpha.1')))
    self.assertTrue(release_info.is_latest(tags, p('0.0.2-alpha.1')))
    # A fix on an older line, tagged after a newer release.
    self.assertFalse(release_info.is_latest(tags, p('0.0.1')))
    self.assertTrue(release_info.is_latest([], p('0.0.1')))

  def test_bundle_must_go_up(self):
    before_version = release_info.bundle_at(None, '158.0.8127.18\nabc\n')
    self.assertEqual(before_version, '8127.18')
    first = release_info.bundle_at('0.0.2-alpha.1\n', '158.0.8127.18\n')
    release_info.check_bundle_goes_up(first, before_version, 'v0.0.1-alpha.1')
    # A newer Chromium point release with the next Koan version.
    second = release_info.bundle_at('0.0.2-alpha.2', '158.0.8127.40')
    release_info.check_bundle_goes_up(second, first, 'v0.0.2-alpha.1')
    # A newer Koan on an older Chromium than the release before.
    older = release_info.bundle_at('0.0.3-alpha.1', '158.0.8127.30')
    with self.assertRaisesRegex(release_info.ReleaseError, 'older Chromium'):
      release_info.check_bundle_goes_up(older, second, 'v0.0.2-alpha.2')
    with self.assertRaises(release_info.ReleaseError):
      release_info.check_bundle_goes_up(first, first, 'v0.0.2-alpha.1')

  def test_info(self):
    v = koan_version.parse('0.0.2-alpha.1')
    values = release_info.info(v, CHROMIUM, 'docs/releases/0.0.2-alpha.1.md',
                               'v0.0.1-alpha.1')
    self.assertEqual(values['title'],
                     'Koan 0.0.2 (Alpha) · Chromium 158.0.8127.18')
    self.assertEqual(values['bundle'], '8127.18.2101')
    final = release_info.info(koan_version.parse('1.0.0'), CHROMIUM, 'n', '')
    self.assertEqual(final['label'], '')

  def test_render_notes(self):
    sha = 'ab' * 32
    body = release_info.render_notes('Get **@DMG@**\n\n```\n@SHA256@  @DMG@\n```\n',
                                     'Koan-0.0.2-alpha.1-arm64.dmg', sha,
                                     "## What's Changed\n* x\n")
    self.assertIn('Get **Koan-0.0.2-alpha.1-arm64.dmg**', body)
    self.assertIn(sha + '  Koan-0.0.2-alpha.1-arm64.dmg', body)
    self.assertTrue(body.endswith("---\n\n## What's Changed\n* x\n"))
    self.assertNotIn('---', release_info.render_notes('x', 'd', sha, ' \n'))
    with self.assertRaises(release_info.ReleaseError):
      release_info.render_notes('x', 'd', 'not-a-sha')

  def test_check_in_a_repository(self):
    with tempfile.TemporaryDirectory() as tmp:
      repo = pathlib.Path(tmp)

      def git(*args):
        subprocess.run(['git', '-C', str(repo), '-c', 'user.name=t', '-c',
                        'user.email=t@example.invalid', *args],
                       check=True, capture_output=True)

      git('init', '-q')
      (repo / 'CHROMIUM_VERSION').write_text('158.0.8127.18\nabc\n')
      git('add', '.')
      git('commit', '-qm', 'first')
      git('tag', 'v0.0.1-alpha.1')
      (repo / 'VERSION').write_text('0.0.2-alpha.1\n')
      notes = repo / 'docs' / 'releases' / '0.0.2-alpha.1.md'
      notes.parent.mkdir(parents=True)
      notes.write_text('Notes\n')
      git('add', '.')
      git('commit', '-qm', 'second')

      class Args:
        tag = 'v0.0.2-alpha.1'
        root = str(repo)

      values = release_info._check(Args)
      self.assertEqual(values['previous_tag'], 'v0.0.1-alpha.1')
      self.assertEqual(values['notes'], 'docs/releases/0.0.2-alpha.1.md')
      Args.tag = 'v0.0.2'
      with self.assertRaises(release_info.ReleaseError):
        release_info._check(Args)


if __name__ == '__main__':
  unittest.main()
