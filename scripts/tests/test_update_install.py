#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""What the Mac's self-update (#550) needs releases to keep stable.

The app pins the release certificate it checks a staged copy against, runs
the helper from Contents/Resources/bin, and installs only a plainly named
.dmg whose app says its KoanVersion. These tests check that the code, the
bundle, the packaging and the release workflow agree on each, so a change to
one side fails here instead of on users' Macs (docs/RELEASING.md, "What
updates rely on").
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
UPDATES = ROOT / 'src' / 'browser' / 'updates'


def _read(path):
  return (ROOT / path).read_text()


def _cpp_string(header, name):
  match = re.search(r'%s\[\] =\s*"([^"]*)";' % re.escape(name), header)
  if not match:
    raise AssertionError('no %s' % name)
  return match.group(1)


class UpdateInstallTest(unittest.TestCase):

  def test_pins_the_release_certificate(self):
    header = (UPDATES / 'koan_update_install_logic.h').read_text()
    pinned = _cpp_string(header, 'kReleaseCertificateSha1')
    identity = _read('scripts/ci/codesign_identity.txt').strip().lower()
    self.assertEqual(pinned, identity)
    self.assertRegex(pinned, r'^[0-9a-f]{40}$')

  def test_bundles_and_signs_the_helper(self):
    logic = (UPDATES / 'koan_update_install_logic.h').read_text()
    helper = _cpp_string(logic, 'kHelperName')
    self.assertEqual(helper, 'koan-update-helper')
    self.assertIn('executable("%s")' % helper,
                  (UPDATES / 'BUILD.gn').read_text())
    # Where StartHelper() looks for it (HelperPathIn()), and where the helper
    # finds the app it may replace (HelperBundle()).
    logic_cc = (UPDATES / 'koan_update_install_logic.cc').read_text()
    self.assertIn('.Append("Resources")\n      .Append("bin")\n'
                  '      .Append(kHelperName)', logic_cc)
    mac = (UPDATES / 'koan_update_platform_mac.mm').read_text()
    self.assertIn('HelperPathIn(base::apple::OuterBundlePath())', mac)
    cli = _read('src/cli/BUILD.gn')
    bundle = cli[cli.index('bundle_data("koan_bundle_data")'):]
    bundle = bundle[:bundle.index('\n  }\n')]
    self.assertIn('"$root_out_dir/%s"' % helper, bundle)
    self.assertIn('"//koan/browser/updates:%s"' % helper, bundle)
    self.assertIn('{{bundle_resources_dir}}/bin/', bundle)
    # The release's signature covers it as code of its own.
    package = _read('scripts/ci/mac_package.sh')
    loop = re.search(r'^for tool in ([^;]+); do$', package, re.MULTILINE)
    self.assertIsNotNone(loop)
    self.assertIn(helper, loop.group(1).split())

  def test_release_names_a_disk_image_the_app_installs(self):
    package = _read('scripts/ci/mac_package.sh')
    # KOAN_RELEASE=1: Koan-<version>-arm64.dmg, which DownloadFileName()
    # takes (a plain name ending in .dmg), with the app at the image's root
    # (FindApp()) and its Info.plist's KoanVersion checked against VERSION.
    self.assertIn('dmg="$name-$version-arm64.dmg"', package)
    self.assertIn('ditto "$app" "$stage/$name.app"', package)
    self.assertIn('plutil -extract KoanVersion', package)
    release = _read('.github/workflows/release.yml')
    self.assertIn('update_manifest.py make --platform mac-arm64', release)
    self.assertIn('releases/download/$TAG/$DMG', release)


if __name__ == '__main__':
  unittest.main()
