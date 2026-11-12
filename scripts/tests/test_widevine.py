#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for Koan's Widevine build pieces (issue #315).

src/build/widevine_signature_placeholder.py (the build's stand-in for
Google's signer), scripts/mac/widevine_vmp_sign.sh without an EVS login (it
must never fail a build that has no secrets), and the hook patches and GN
args that wire them up.
"""

import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src' / 'build'))
import widevine_signature_placeholder  # noqa: E402

SIGN = ROOT / 'scripts' / 'mac' / 'widevine_vmp_sign.sh'


class PlaceholderTest(unittest.TestCase):

  def test_writes_an_empty_signature(self):
    with tempfile.TemporaryDirectory() as tmp:
      out = pathlib.Path(tmp) / 'Koan Framework.sig'
      out.write_bytes(b'stale')
      rc = widevine_signature_placeholder.main([
          '--input_file', 'Koan Framework', '--output_file', str(out),
          '--flags', '1', '--ignore_missing_cert'
      ])
      self.assertEqual(rc, 0)
      self.assertEqual(out.read_bytes(), b'')

  def test_widevine_gni_patch_points_at_it(self):
    patch = (ROOT / 'patches/third_party/widevine/cdm/widevine.gni.patch'
             ).read_text()
    self.assertIn('+    script = "//koan/build/'
                  'widevine_signature_placeholder.py"', patch)


class VmpSignWithoutLoginTest(unittest.TestCase):

  def run_sign(self, *args):
    with tempfile.TemporaryDirectory() as tmp:
      app = pathlib.Path(tmp) / 'Koan.app'
      fw = (app / 'Contents/Frameworks/Koan Framework.framework/Versions/1.0')
      (fw / 'Resources').mkdir(parents=True)
      (fw / 'Koan Framework').write_bytes(b'\0')
      (fw.parent / 'Current').symlink_to('1.0')
      env = {
          k: v for k, v in os.environ.items()
          if not k.startswith('EVS_') and k != 'CI'
      }
      env['EVS_CONFIG_FILE'] = str(pathlib.Path(tmp) / 'no-login.json')
      env['KOAN_MAC'] = tmp
      return subprocess.run(['bash', str(SIGN), *args, str(app)],
                            env=env,
                            capture_output=True,
                            text=True,
                            check=False)

  def test_if_configured_skips_quietly(self):
    r = self.run_sign('--if-configured')
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertIn('no EVS login', r.stdout)

  def test_asked_to_sign_without_login_fails(self):
    r = self.run_sign()
    self.assertEqual(r.returncode, 1)
    self.assertIn('castlabs_evs.account signup', r.stderr)


class MacArgsTest(unittest.TestCase):

  def test_mac_build_enables_widevine_and_host_verification(self):
    args = (ROOT / 'build/koan-mac.gn').read_text().splitlines()
    self.assertIn('enable_widevine = true', args)
    self.assertIn('enable_cdm_host_verification = true', args)


if __name__ == '__main__':
  unittest.main()
