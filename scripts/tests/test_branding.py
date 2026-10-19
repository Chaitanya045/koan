#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/branding.py (the product-name and icon rewrites)."""

import pathlib
import re
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import branding  # noqa: E402


def message(body, name='IDS_X', desc='Chromium does things'):
  return f'<message name="{name}" desc="{desc}">{body}</message>'


class RebrandMessagesTest(unittest.TestCase):

  def test_replaces_product_name_in_body(self):
    self.assertEqual(
        branding.rebrand_messages(message('Relaunch Chromium now')),
        message('Relaunch Koan now'))

  def test_leaves_attributes_alone(self):
    out = branding.rebrand_messages(message('x', desc='About Chromium'))
    self.assertIn('desc="About Chromium"', out)

  def test_leaves_text_outside_messages_alone(self):
    text = '<!-- Chromium strings -->' + message('Chromium')
    out = branding.rebrand_messages(text)
    self.assertTrue(out.startswith('<!-- Chromium strings -->'))
    self.assertIn('>Koan<', out)

  def test_keeps_project_credit(self):
    for body in ('The Chromium Authors',
                 'the Chromium open source project',
                 'made possible by the Chromium project'):
      with self.subTest(body=body):
        self.assertEqual(branding.rebrand_messages(message(body)),
                         message(body))

  def test_keeps_linked_project_name(self):
    body = ('Koan is made possible by the <ph name="BEGIN_LINK_CHROMIUM">'
            '&lt;a&gt;</ph>Chromium<ph name="END_LINK_CHROMIUM">&lt;/a&gt;'
            '</ph> open source project.')
    self.assertEqual(branding.rebrand_messages(message(body)), message(body))

  def test_keeps_chromiumos(self):
    self.assertEqual(branding.rebrand_messages(message('ChromiumOS update')),
                     message('ChromiumOS update'))

  def test_multiline_bodies(self):
    self.assertEqual(
        branding.rebrand_messages(message('\n  Chromium is\n  ready\n')),
        message('\n  Koan is\n  ready\n'))

  def test_idempotent(self):
    once = branding.rebrand_messages(message('Chromium and Chromium'))
    self.assertEqual(branding.rebrand_messages(once), once)


class IconTest(unittest.TestCase):

  def test_enso_path_stays_in_box(self):
    for size, offset in ((24, 0), (960, -960)):
      with self.subTest(size=size):
        numbers = [
            float(n)
            for n in re.findall(r'-?\d+\.\d+', branding.enso_path(size, offset))
        ]
        # Radii appear as numbers too; every number must fit the viewBox.
        for n in numbers:
          self.assertGreaterEqual(n, offset - 1e-6)
          self.assertLessEqual(n, size + 1e-6)

  def test_replaces_both_icon_variants(self):
    text = ('<g id="chrome-product">\n        <path d="OLD1"></path>\n'
            '<g id="chrome-product" viewBox="0 -960 960 960">\n'
            '        <path d="OLD2">\n')
    out = branding.rebrand_cr_icons(text)
    self.assertNotIn('OLD1', out)
    self.assertNotIn('OLD2', out)
    self.assertIn(branding.enso_path(24), out)
    self.assertIn(branding.enso_path(960, -960), out)

  def test_other_icons_untouched(self):
    text = '<g id="close">\n  <path d="KEEP"></path>'
    self.assertEqual(branding.rebrand_cr_icons(text), text)


class ManifestTest(unittest.TestCase):

  def test_logo_artifacts_come_from_rendered_set(self):
    rendered = {
        'logo_16.png', 'logo_22_mono.png', 'logo_24.png', 'logo_32.png',
        'logo_32.xpm', 'logo_48.png', 'logo_64.png', 'logo_128.png',
        'logo_256.png', 'wordmark_22.png', 'wordmark_22_white.png',
        'wordmark_44.png', 'wordmark_44_white.png'
    }
    self.assertLessEqual(set(branding.LOGOS.values()), rendered)

  def test_branded_files_are_unique(self):
    self.assertEqual(len(branding.BRANDED_FILES),
                     len(set(branding.BRANDED_FILES)))


if __name__ == '__main__':
  unittest.main()
