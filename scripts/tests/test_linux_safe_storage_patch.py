#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Koan's key for cookies and passwords is its own keyring item on Linux.

Upstream names the item in one place, the non-Chrome-branded constants of
components/os_crypt/async/browser/freedesktop_secret_key_provider.h
("Chromium Safe Storage", application "chromium", KWallet folder "Chromium
Keys"). Koan's patch swaps exactly those three for koan_branding.h's. This
test pins what the patch is allowed to do, Koan's values (renaming them
would make every profile's data undecryptable) and the docs. If upstream
renames or moves the constants, the patch stops applying
(scripts/ci/check_patches.sh); KoanLinuxSafeStorageTest in koan_unittests
checks the compiled provider.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
UPSTREAM = 'components/os_crypt/async/browser/freedesktop_secret_key_provider.h'
PATCH = ROOT / 'patches' / (UPSTREAM + '.patch')
BUILD_PATCH = ROOT / 'patches/components/os_crypt/async/browser/BUILD.gn.patch'
BRANDING = ROOT / 'src/common/koan_branding.h'


def changes(path):
  lines = path.read_text().splitlines()
  body = [l for l in lines if not l.startswith(('---', '+++'))]
  return (lines, [l[1:] for l in body if l.startswith('-')],
          [l[1:] for l in body if l.startswith('+')])


class LinuxSafeStoragePatchTest(unittest.TestCase):

  def test_patch_is_for_the_one_file(self):
    lines, _, _ = changes(PATCH)
    self.assertEqual(lines[0], 'diff --git a/%s b/%s' % (UPSTREAM, UPSTREAM))
    self.assertEqual([l for l in lines if l.startswith('diff --git')],
                     [lines[0]])

  def test_swaps_exactly_chromiums_three_names(self):
    _, removed, added = changes(PATCH)
    self.assertEqual(removed, [
        '  static constexpr char kKWalletFolder[] = "Chromium Keys";',
        '  static constexpr char kKeyName[] = "Chromium Safe Storage";',
        '  static constexpr char kAppName[] = "chromium";',
    ])
    self.assertEqual(
        [l for l in added if not l.lstrip().startswith('//')], [
            '#include "koan/common/koan_branding.h"',
            '  static constexpr auto& kKWalletFolder = koan::kLinuxKWalletFolder;',
            '  static constexpr auto& kKeyName = koan::kLinuxSecretKeyName;',
            '  static constexpr auto& kAppName = koan::kLinuxSecretAppName;',
        ])

  def test_only_the_non_branded_branch(self):
    # Google Chrome's names stay in the BUILDFLAG(GOOGLE_CHROME_BRANDING)
    # branch, as context; Koan's replace the #else branch.
    text = PATCH.read_text()
    self.assertRegex(
        text, r'  static constexpr char kAppName\[\] = "chrome";\n #else\n'
        r'(\+.*\n)?-  static constexpr char kKWalletFolder')
    self.assertIn('\n #endif\n', text)

  def test_build_file_adds_the_branding_dep_only(self):
    lines, removed, added = changes(BUILD_PATCH)
    self.assertEqual(removed, [])
    self.assertEqual(added, ['      "//koan/common:branding",'])
    self.assertIn('     public_deps = [ "//build:branding_buildflags" ]',
                  lines)

  def test_koan_values_never_change(self):
    text = BRANDING.read_text()
    for name, value in [
        ('kLinuxSecretKeyName', 'Koan Safe Storage'),
        ('kLinuxSecretAppName', 'koan'),
        ('kLinuxKWalletFolder', 'Koan Keys'),
        # The Mac's, for the same reason.
        ('kMacKeychainServiceName', 'Koan Safe Storage'),
        ('kMacKeychainAccountName', 'Koan'),
    ]:
      self.assertRegex(
          text, r'inline constexpr char %s\[\] = "%s";' %
          (name, re.escape(value)))

  def test_docs_mention_it(self):
    rebase = (ROOT / 'docs/REBASE.md').read_text()
    self.assertIn(UPSTREAM, rebase)
    self.assertIn('test_linux_safe_storage_patch.py', rebase)
    self.assertIn('KoanLinuxSafeStorageTest', rebase)
    usage = (ROOT / 'docs/USAGE.md').read_text()
    self.assertIn('"Koan Safe Storage"', usage)
    self.assertIn('"Koan Keys"', usage)
    self.assertIn('"Chromium Safe Storage"', usage)


if __name__ == '__main__':
  unittest.main()
