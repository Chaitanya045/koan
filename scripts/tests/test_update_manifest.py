#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/ci/update_manifest.py, with throwaway Ed25519 keys."""

import base64
import contextlib
import hashlib
import io
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts' / 'ci'))
import update_manifest  # noqa: E402

PUBLIC_KEY_FILE = ROOT / 'src' / 'browser' / 'updates' / (
    'koan_update_public_key.txt')
VERSION = '0.0.3-alpha.1'
CHROMIUM = '158.0.8127.18'
NOTES = 'https://github.com/koan-browser/koan/releases/tag/v0.0.3-alpha.1'
URL = 'https://github.com/koan-browser/koan/releases/download/v0.0.3-alpha.1/'


def run_main(args):
  """update_manifest.main(args), with its messages swallowed."""
  with contextlib.redirect_stdout(io.StringIO()), \
      contextlib.redirect_stderr(io.StringIO()):
    return update_manifest.main(args)


def _generate_key(path):
  result = subprocess.run(
      ['openssl', 'genpkey', '-algorithm', 'ED25519', '-out', str(path)],
      capture_output=True)
  return result.returncode == 0


class MessageTest(unittest.TestCase):

  def test_fixed_vector(self):
    data = update_manifest.message('mac-arm64', VERSION, 'ab' * 32, 123456789)
    self.assertEqual(
        data, b'koan-update-v1\nmac-arm64\n0.0.3-alpha.1\n' + b'ab' * 32 +
        b'\n123456789\n')

  def test_cli_prints_the_same_bytes(self):
    result = subprocess.run([
        sys.executable, '-I',
        str(ROOT / 'scripts' / 'ci' / 'update_manifest.py'), 'message',
        '--platform', 'linux-x86_64', '--version', '1.0.0', '--sha256',
        '0' * 64, '--size', '7'
    ], capture_output=True)
    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertEqual(
        result.stdout, b'koan-update-v1\nlinux-x86_64\n1.0.0\n' + b'0' * 64 +
        b'\n7\n')

  def test_rejects_bad_fields(self):
    good = ('mac-arm64', VERSION, 'ab' * 32, 1)
    update_manifest.message(*good)
    for index, value in ((0, 'windows'), (0, 'mac-arm64\nx'),
                         (1, '0.0.3'+'\n'), (1, 'v0.0.3'), (1, '0.0.3-dev.1'),
                         (2, 'AB' * 32), (2, 'ab' * 31), (3, -1),
                         (3, '1')):
      bad = list(good)
      bad[index] = value
      with self.assertRaises(update_manifest.ManifestError, msg=bad):
        update_manifest.message(*bad)


class PublicKeyTest(unittest.TestCase):

  def test_committed_key_is_32_bytes(self):
    text = PUBLIC_KEY_FILE.read_text()
    self.assertTrue(text.endswith('\n'))
    self.assertEqual(len(base64.b64decode(text.strip(), validate=True)), 32)
    self.assertEqual(len(update_manifest.read_public_key(PUBLIC_KEY_FILE)), 32)


class ManifestTest(unittest.TestCase):

  @classmethod
  def setUpClass(cls):
    cls.tmp = tempfile.TemporaryDirectory()
    cls.dir = pathlib.Path(cls.tmp.name)
    cls.key = cls.dir / 'key.pem'
    cls.other_key = cls.dir / 'other.pem'
    try:
      ok = _generate_key(cls.key) and _generate_key(cls.other_key)
    except FileNotFoundError:
      ok = False
    if not ok:
      cls.tmp.cleanup()
      raise unittest.SkipTest('openssl with Ed25519 is not available')
    cls.public = update_manifest.public_key_of(cls.key)
    cls.other_public = update_manifest.public_key_of(cls.other_key)

  @classmethod
  def tearDownClass(cls):
    cls.tmp.cleanup()

  def setUp(self):
    self.work = tempfile.TemporaryDirectory()
    self.addCleanup(self.work.cleanup)
    self.path = pathlib.Path(self.work.name)
    self.dmg = self.path / 'Koan.dmg'
    self.dmg.write_bytes(b'not really a disk image\n' * 100000)
    self.manifest = self.path / 'koan-update.json'

  def make(self, platform='mac-arm64', file=None, version=VERSION,
           chromium=CHROMIUM):
    file = file or self.dmg
    code = run_main([
        'make', '--platform', platform, '--file', str(file), '--url',
        URL + file.name, '--version', version, '--chromium', chromium,
        '--notes-url', NOTES, '--key-file', str(self.key), '--output',
        str(self.manifest)
    ])
    self.assertEqual(code, 0)

  def load(self):
    return json.loads(self.manifest.read_text())

  def save(self, manifest):
    self.manifest.write_text(update_manifest.dump(manifest))

  def verify(self, file=None, platform='mac-arm64', public=None):
    update_manifest.verify(self.manifest, file or self.dmg, platform,
                           public or self.public)

  def assert_fails(self, **kwargs):
    with self.assertRaises(update_manifest.ManifestError):
      self.verify(**kwargs)

  def test_round_trip(self):
    self.make()
    text = self.manifest.read_text()
    self.assertTrue(text.endswith('}\n'))
    self.assertEqual(text, update_manifest.dump(json.loads(text)))
    manifest = self.load()
    self.assertEqual(manifest['schema'], 1)
    self.assertEqual(manifest['version'], VERSION)
    self.assertEqual(manifest['chromium'], CHROMIUM)
    self.assertEqual(manifest['notes_url'], NOTES)
    item = manifest['platforms']['mac-arm64']
    self.assertEqual(item['url'], URL + 'Koan.dmg')
    self.assertEqual(item['size'], self.dmg.stat().st_size)
    self.assertEqual(item['sha256'],
                     hashlib.sha256(self.dmg.read_bytes()).hexdigest())
    self.assertEqual(len(base64.b64decode(item['signature'])), 64)
    self.verify()

  def test_verify_through_the_cli_with_a_key_file(self):
    self.make()
    key_file = self.path / 'public.txt'
    key_file.write_text(base64.b64encode(self.public).decode() + '\n')
    script = str(ROOT / 'scripts' / 'ci' / 'update_manifest.py')
    args = [
        sys.executable, '-I', script, 'verify', '--manifest',
        str(self.manifest), '--file', str(self.dmg), '--platform',
        'mac-arm64', '--public-key-file', str(key_file)
    ]
    self.assertEqual(subprocess.run(args, capture_output=True).returncode, 0)
    self.dmg.write_bytes(self.dmg.read_bytes() + b'x')
    result = subprocess.run(args, capture_output=True)
    self.assertEqual(result.returncode, 1)
    self.assertIn(b'size mismatch', result.stderr)

  def test_changed_file_fails(self):
    self.make()
    data = bytearray(self.dmg.read_bytes())
    data[len(data) // 2] ^= 1
    self.dmg.write_bytes(bytes(data))
    self.assert_fails()

  def test_changed_size_fails(self):
    self.make()
    manifest = self.load()
    manifest['platforms']['mac-arm64']['size'] += 1
    self.save(manifest)
    self.assert_fails()

  def test_size_with_a_matching_file_but_a_stale_signature_fails(self):
    self.make()
    manifest = self.load()
    self.dmg.write_bytes(self.dmg.read_bytes() + b'x')
    item = manifest['platforms']['mac-arm64']
    item['size'] += 1
    item['sha256'] = hashlib.sha256(self.dmg.read_bytes()).hexdigest()
    self.save(manifest)
    self.assert_fails()

  def test_changed_sha256_fails(self):
    self.make()
    manifest = self.load()
    manifest['platforms']['mac-arm64']['sha256'] = '0' * 64
    self.save(manifest)
    self.assert_fails()

  def test_changed_version_fails(self):
    # An older signed image labelled as a newer version.
    self.make()
    manifest = self.load()
    manifest['version'] = '0.0.4-alpha.1'
    self.save(manifest)
    self.assert_fails()

  def test_changed_platform_fails(self):
    self.make()
    manifest = self.load()
    manifest['platforms']['linux-x86_64'] = manifest['platforms'].pop(
        'mac-arm64')
    self.save(manifest)
    self.assert_fails(platform='linux-x86_64')

  def test_missing_platform_fails(self):
    self.make()
    self.assert_fails(platform='linux-x86_64')

  def test_changed_signature_fails(self):
    self.make()
    manifest = self.load()
    item = manifest['platforms']['mac-arm64']
    signature = bytearray(base64.b64decode(item['signature']))
    signature[0] ^= 1
    item['signature'] = base64.b64encode(bytes(signature)).decode()
    self.save(manifest)
    self.assert_fails()
    item['signature'] = 'not base64!'
    self.save(manifest)
    self.assert_fails()
    item['signature'] = base64.b64encode(b'short').decode()
    self.save(manifest)
    self.assert_fails()

  def test_wrong_public_key_fails(self):
    self.make()
    self.assert_fails(public=self.other_public)

  def test_adds_a_second_platform_to_the_same_release(self):
    self.make()
    linux = self.path / 'Koan.tar.xz'
    linux.write_bytes(b'linux build' * 1000)
    self.make(platform='linux-x86_64', file=linux)
    manifest = self.load()
    self.assertEqual(sorted(manifest['platforms']),
                     ['linux-x86_64', 'mac-arm64'])
    self.verify()
    self.verify(file=linux, platform='linux-x86_64')

  def test_replaces_a_platform_of_the_same_release(self):
    self.make()
    first = self.load()['platforms']['mac-arm64']
    self.dmg.write_bytes(b'rebuilt')
    self.make()
    manifest = self.load()
    self.assertEqual(list(manifest['platforms']), ['mac-arm64'])
    self.assertNotEqual(manifest['platforms']['mac-arm64']['sha256'],
                        first['sha256'])
    self.verify()

  def test_a_different_release_starts_a_fresh_manifest(self):
    self.make()
    linux = self.path / 'Koan.tar.xz'
    linux.write_bytes(b'linux build' * 1000)
    self.make(platform='linux-x86_64', file=linux, version='0.0.4-alpha.1')
    manifest = self.load()
    self.assertEqual(manifest['version'], '0.0.4-alpha.1')
    self.assertEqual(list(manifest['platforms']), ['linux-x86_64'])
    self.make(platform='mac-arm64', chromium='158.0.8127.19')
    manifest = self.load()
    self.assertEqual(list(manifest['platforms']), ['mac-arm64'])

  def test_rejects_bad_inputs_without_writing(self):
    for version in ('0.0.3-alpha', 'v0.0.3', '1.2', ''):
      with self.assertRaises(update_manifest.ManifestError, msg=version):
        update_manifest.check_version(version)
      self.assertEqual(
          run_main([
              'make', '--platform', 'mac-arm64', '--file', str(self.dmg),
              '--url', URL, '--version', version, '--chromium', CHROMIUM,
              '--notes-url', NOTES, '--key-file', str(self.key), '--output',
              str(self.manifest)
          ]), 1)
      self.assertFalse(self.manifest.exists())
    for platform in ('windows', 'mac-x86_64'):
      with self.assertRaises(update_manifest.ManifestError, msg=platform):
        update_manifest.check_platform(platform)
    with self.assertRaises(update_manifest.ManifestError):
      update_manifest.check_chromium('158.0.8127')
    with self.assertRaises(update_manifest.ManifestError):
      update_manifest.check_url('http://example.com/x.dmg')

  def test_make_with_a_missing_key_fails(self):
    self.assertEqual(
        run_main([
            'make', '--platform', 'mac-arm64', '--file', str(self.dmg),
            '--url', URL, '--version', VERSION, '--chromium', CHROMIUM,
            '--notes-url', NOTES, '--key-file',
            str(self.path / 'missing.pem'), '--output',
            str(self.manifest)
        ]), 1)
    self.assertFalse(self.manifest.exists())


if __name__ == '__main__':
  unittest.main()
