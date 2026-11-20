#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/branding.py (the product-name and icon rewrites)."""

import json
import pathlib
import plistlib
import re
import sys
import tempfile
import unittest
from unittest import mock

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

  def test_mark_path_stays_in_box(self):
    for size, offset in ((24, 0), (960, -960)):
      with self.subTest(size=size):
        numbers = [
            float(n)
            for n in re.findall(r'-?\d+\.\d+', branding.mark_path(size, offset))
        ]
        # Radii appear as numbers too; every number must fit the viewBox.
        for n in numbers:
          self.assertGreaterEqual(n, offset - 1e-6)
          self.assertLessEqual(n, size + 1e-6)

  def test_mark_path_is_centred(self):
    for size, offset in ((24, 0), (960, -960)):
      with self.subTest(size=size):
        points = [(float(x), float(y)) for x, y in re.findall(
            r'[ML](-?\d+\.\d+) (-?\d+\.\d+)', branding.mark_path(
                size, offset))]
        xs = [x for x, _ in points]
        ys = [y - offset for _, y in points]
        # Arc caps bulge past their end points, so allow one cap radius.
        cap = branding.MARK_STEM[2] * size / 100
        self.assertAlmostEqual((min(xs) + max(xs)) / 2, size / 2, delta=cap)
        self.assertAlmostEqual((min(ys) + max(ys)) / 2, size / 2, delta=cap)

  def test_mark_subpaths_wind_the_same_way(self):
    # Nonzero fill draws the union only if every subpath is clockwise.
    for sub in branding.mark_path(100).split('Z')[:-1]:
      points = [(float(x), float(y))
                for x, y in re.findall(r'(-?\d+\.\d+) (-?\d+\.\d+)', sub)]
      points = [p for i, p in enumerate(points) if i == 0 or p != points[i - 1]]
      area = sum(x1 * y2 - x2 * y1 for (x1, y1), (x2, y2) in zip(
          points, points[1:] + points[:1]))
      with self.subTest(start=sub.strip()[:20]):
        self.assertGreater(area, 0)  # clockwise on screen (y down)

  def test_mark_matches_logo(self):
    # The cr icon and the SVGs draw the same pebble.
    svg = (ROOT / 'branding' / 'logo.svg').read_text()
    x, y = branding.pebble_points()[0]
    self.assertIn(f'M{x:.2f} {y:.2f} L', svg)

  def test_replaces_both_icon_variants(self):
    text = ('<g id="chrome-product">\n        <path d="OLD1"></path>\n'
            '<g id="chrome-product" viewBox="0 -960 960 960">\n'
            '        <path d="OLD2">\n')
    out = branding.rebrand_cr_icons(text)
    self.assertNotIn('OLD1', out)
    self.assertNotIn('OLD2', out)
    self.assertIn(branding.mark_path(24), out)
    self.assertIn(branding.mark_path(960, -960), out)

  def test_other_icons_untouched(self):
    text = '<g id="close">\n  <path d="KEEP"></path>'
    self.assertEqual(branding.rebrand_cr_icons(text), text)


ROOT = pathlib.Path(__file__).resolve().parent.parent.parent

# What scripts/apply_branding.sh renders (and, on macOS, builds).
RENDERED = {
    'logo_16.png', 'logo_22_mono.png', 'logo_24.png', 'logo_32.png',
    'logo_small_16.png', 'logo_small_32.png', 'logo_small_64.png',
    'logo_48.png', 'logo_64.png', 'logo_128.png', 'logo_256.png',
    'logo_512.png', 'wordmark_22.png', 'wordmark_22_white.png',
    'wordmark_44.png', 'wordmark_44_white.png'
}
LINUX_ONLY = {'logo_32.xpm'}
MAC_ONLY = {'mac/app.icns', 'mac/Assets.car'}


class ManifestTest(unittest.TestCase):

  def test_logo_artifacts_come_from_rendered_set(self):
    self.assertLessEqual(set(branding.logos_for('linux').values()),
                         RENDERED | LINUX_ONLY)
    self.assertLessEqual(set(branding.logos_for('mac').values()),
                         RENDERED | MAC_ONLY)

  def test_linux_keeps_desktop_icons_and_skips_mac_icon(self):
    linux = branding.logos_for('linux')
    self.assertIn('chrome/app/theme/chromium/linux/product_logo_32.xpm', linux)
    self.assertIn('chrome/app/theme/chromium/linux/product_logo_256.png', linux)
    self.assertFalse([rel for rel in linux if '/mac/' in rel])

  def test_mac_brands_app_icon_and_skips_linux_icons(self):
    mac = branding.logos_for('mac')
    self.assertEqual(mac['chrome/app/theme/chromium/mac/app.icns'],
                     'mac/app.icns')
    self.assertEqual(mac['chrome/app/theme/chromium/mac/Assets.car'],
                     'mac/Assets.car')
    self.assertFalse([rel for rel in mac if '/linux/' in rel])
    self.assertNotIn('logo_32.xpm', mac.values())

  def test_small_product_logos_use_logo_small(self):
    # IDR_PRODUCT_LOGO_16/32 at 1x and 2x: the crisp small tile.
    for scale, sizes in (('100', (16, 32)), ('200', (32, 64))):
      for name, px in zip(('16', '32'), sizes):
        rel = (f'chrome/app/theme/default_{scale}_percent/chromium/'
               f'product_logo_{name}.png')
        with self.subTest(rel=rel):
          self.assertEqual(branding.LOGOS[rel], f'logo_small_{px}.png')

  def test_shared_logos_on_both_platforms(self):
    for platform in branding.PLATFORMS:
      with self.subTest(platform=platform):
        self.assertLessEqual(set(branding.LOGOS),
                             set(branding.logos_for(platform)))

  def test_branded_files_cover_every_platform(self):
    for platform in branding.PLATFORMS:
      with self.subTest(platform=platform):
        self.assertLessEqual(set(branding.logos_for(platform)),
                             set(branding.BRANDED_FILES))

  def test_webui_dark_logo_is_koans(self):
    # cr-toolbar's dark-mode logo (Settings, History, Downloads, Extensions).
    rel = 'ui/webui/resources/images/chrome_logo_dark.svg'
    self.assertEqual(branding.SVG_LOGOS[rel], 'logo.svg')
    self.assertIn(rel, branding.BRANDED_FILES)
    for source in branding.SVG_LOGOS.values():
      self.assertTrue((ROOT / 'branding' / source).is_file(), source)

  def test_branded_files_are_unique(self):
    self.assertEqual(len(branding.BRANDED_FILES),
                     len(set(branding.BRANDED_FILES)))


class MacIconTest(unittest.TestCase):

  def test_icon_pixels(self):
    self.assertEqual(branding.mac_icon_pixels(),
                     [16, 32, 64, 128, 256, 512, 1024])

  def test_iconset_names_are_what_iconutil_expects(self):
    names = {
        branding.mac_iconset_name(points, scale)
        for points, scale in branding.MAC_ICON_SET
    }
    expected = set()
    for points in (16, 32, 128, 256, 512):
      expected.add(f'icon_{points}x{points}.png')
      expected.add(f'icon_{points}x{points}@2x.png')
    self.assertEqual(names, expected)

  def test_appiconset_uses_rendered_bitmaps(self):
    contents = branding.appiconset_contents()
    rendered = {f'appicon_{px}.png' for px in branding.mac_icon_pixels()}
    self.assertEqual(len(contents['images']), len(branding.MAC_ICON_SET))
    for image in contents['images']:
      with self.subTest(image=image):
        self.assertIn(image['filename'], rendered)
        self.assertEqual(image['idiom'], 'mac')
        points = int(image['size'].split('x')[0])
        scale = int(image['scale'][0])
        self.assertEqual(image['filename'], f'appicon_{points * scale}.png')

  def test_logo_body_matches_logo_svg(self):
    svg = (ROOT / 'branding' / 'logo.svg').read_text()
    x, y, size = branding.LOGO_BODY
    self.assertIn(branding.LOGO_VIEW_BOX, svg)
    body = re.search(r'<path id="body" d="([^"]+)"', svg).group(1)
    numbers = [float(n) for n in re.findall(r'-?\d+\.?\d*', body)]
    xs, ys = numbers[0::2], numbers[1::2]
    self.assertAlmostEqual(min(xs), x, places=1)
    self.assertAlmostEqual(min(ys), y, places=1)
    self.assertAlmostEqual(max(xs), x + size, places=1)
    self.assertAlmostEqual(max(ys), y + size, places=1)

  def test_icon_svg_puts_body_on_apple_grid(self):
    svg = (ROOT / 'branding' / 'logo.svg').read_text()
    out = branding.mac_icon_svg(svg)
    left, top, width, height = (
        float(n) for n in re.search(r'viewBox="([^"]+)"', out).group(1).split())
    x, y, size = branding.LOGO_BODY
    self.assertAlmostEqual(width, height)
    self.assertAlmostEqual(size / width, 824 / 1024, places=4)
    self.assertAlmostEqual(left + width / 2, x + size / 2, places=2)
    self.assertAlmostEqual(top + height / 2, y + size / 2, places=2)
    # Only the view box changes.
    self.assertEqual(
        re.sub(r'viewBox="[^"]+"', '', out), re.sub(r'viewBox="[^"]+"', '',
                                                    svg))

  def test_icon_svg_needs_known_view_box(self):
    with self.assertRaises(ValueError):
      branding.mac_icon_svg('<svg viewBox="0 0 10 10"></svg>')

  def test_icon_composer_document(self):
    # actool names the compiled icon after the .icon file; Info.plist's
    # CFBundleIconName is "AppIcon".
    self.assertEqual(branding.MAC_APP_ICON.name, 'AppIcon.icon')
    doc = json.loads((branding.MAC_APP_ICON / 'icon.json').read_text())
    images = [
        layer['image-name'] for group in doc['groups']
        for layer in group['layers']
    ]
    self.assertTrue(images)
    for image in images:
      with self.subTest(image=image):
        self.assertTrue((branding.MAC_APP_ICON / 'Assets' / image).is_file())


class BrandingSvgTest(unittest.TestCase):
  """branding/*.svg: the formats apply_branding.sh relies on."""

  def test_wordmark_placeholders(self):
    svg = (ROOT / 'branding' / 'wordmark.svg').read_text()
    width, height = branding.WORDMARK_SIZE
    self.assertIn(f'viewBox="0 0 {width} {height}"', svg)
    self.assertIn('fill="COLOR"', svg)
    self.assertIn('fill="STONE"', svg)

  def test_mono_is_one_black_path(self):
    svg = (ROOT / 'branding' / 'logo_mono.svg').read_text()
    self.assertIn('viewBox="0 0 22 22"', svg)
    self.assertEqual(svg.count('<path'), 1)
    self.assertIn('fill="#000"', svg)

  def test_icon_composer_layers_and_appearances(self):
    doc = json.loads((branding.MAC_APP_ICON / 'icon.json').read_text())
    layers = [layer for group in doc['groups'] for layer in group['layers']]
    self.assertEqual(sorted(layer['name'] for layer in layers),
                     ['ink', 'stone'])
    for fills in [doc['fill-specializations']] + [
        layer['fill-specializations'] for layer in layers
    ]:
      appearances = sorted(f.get('appearance', '') for f in fills)
      self.assertEqual(appearances, ['', 'dark', 'tinted'])


class ActoolTest(unittest.TestCase):

  def setUp(self):
    self.tmp = tempfile.TemporaryDirectory()
    self.addCleanup(self.tmp.cleanup)
    src = pathlib.Path(self.tmp.name)
    gni = src / 'build' / 'config' / 'mac' / 'mac_sdk.gni'
    gni.parent.mkdir(parents=True)
    gni.write_text('declare_args() {\n'
                   '  # The minimum macOS.\n'
                   '  mac_deployment_target = "13.0"\n'
                   '}\n')
    patcher = mock.patch.object(branding, 'SRC', src)
    patcher.start()
    self.addCleanup(patcher.stop)

  def test_deployment_target_from_chromium(self):
    self.assertEqual(branding.mac_deployment_target(), '13.0')

  def test_command_with_icon_composer_icon(self):
    out = pathlib.Path('/out')
    command = branding.actool_command(pathlib.Path('/a.xcassets'), out,
                                      pathlib.Path('/AppIcon.icon'))
    self.assertEqual(command[:2], ['xcrun', 'actool'])
    self.assertEqual(command[-2:], ['/a.xcassets', '/AppIcon.icon'])
    self.assertIn('--app-icon=AppIcon', command)
    self.assertIn('--minimum-deployment-target=13.0', command)
    self.assertIn('--compile=/out', command)
    self.assertIn('--include-all-app-icons', command)

  def test_command_bitmaps_only(self):
    command = branding.actool_command(pathlib.Path('/a.xcassets'),
                                      pathlib.Path('/out'))
    self.assertEqual(command[-1], '/a.xcassets')
    self.assertNotIn('--include-all-app-icons', command)

  def test_problems_from_plist(self):
    output = plistlib.dumps({
        'com.apple.actool.errors': [{
            'description': 'bad'
        }],
        'com.apple.actool.document.warnings': [{
            'message': 'meh'
        }],
        'com.apple.actool.notices': [{
            'message': 'fyi'
        }],
    })
    problems = branding.actool_problems(output)
    # Notices count too: actool reports missing inputs as notices.
    self.assertEqual(len(problems), 3)
    for word in ('bad', 'meh', 'fyi'):
      self.assertTrue(any(word in p for p in problems), word)

  def test_expected_warnings_pass(self):
    output = plistlib.dumps({
        'com.apple.actool.document.warnings': [{
            'type': 'Ambiguous Content',
            'message': 'x'
        }, {
            'type': 'Unsupported Configuration',
            'message': 'Failed to generate flattened icon stack for AppIcon'
        }],
    })
    self.assertEqual(branding.actool_problems(output), [])

  def test_other_document_warnings_fail(self):
    output = plistlib.dumps({
        'com.apple.actool.document.warnings': [{
            'type': 'Unsupported Configuration',
            'message': 'The app icon set has an unassigned child.'
        }],
    })
    self.assertEqual(len(branding.actool_problems(output)), 1)

  def test_problems_from_garbage(self):
    self.assertEqual(branding.actool_problems(b'xcrun: error: no actool'),
                     ['xcrun: error: no actool'])


if __name__ == '__main__':
  unittest.main()


class MacRendersTest(unittest.TestCase):
  """branding/mac/rendered: the PNGs macOS uses, rendered on Linux."""

  def test_committed_set_is_complete(self):
    committed = sorted(p.name for p in branding.MAC_RENDERS.iterdir())
    self.assertEqual(committed, branding.mac_rendered_files())

  def test_committed_sizes(self):
    for name in branding.mac_rendered_files():
      with self.subTest(name=name):
        self.assertEqual(branding.png_size(branding.MAC_RENDERS / name),
                         branding.expected_png_size(name))

  def test_mac_needs_every_shared_logo_and_icon(self):
    names = set(branding.mac_rendered_files())
    self.assertLessEqual(set(branding.LOGOS.values()), names)
    self.assertLessEqual(set(branding.MAC_BADGE.values()), names)
    for px in branding.mac_icon_pixels():
      self.assertIn(f'appicon_{px}.png', names)

  def test_expected_sizes(self):
    self.assertEqual(branding.expected_png_size('logo_22_mono.png'), (22, 22))
    self.assertEqual(branding.expected_png_size('logo_small_64.png'), (64, 64))
    self.assertEqual(branding.expected_png_size('appicon_1024.png'),
                     (1024, 1024))
    self.assertEqual(branding.expected_png_size('wordmark_22.png'), (43, 22))
    self.assertEqual(branding.expected_png_size('wordmark_44_white.png'),
                     (86, 44))
    self.assertIsNone(branding.expected_png_size('other.png'))

  def test_wrong_size_fails(self):
    with tempfile.TemporaryDirectory() as tmp:
      wrong = pathlib.Path(tmp) / 'logo_32.png'
      wrong.write_bytes((branding.MAC_RENDERS / 'logo_16.png').read_bytes())
      with self.assertRaises(SystemExit):
        branding.check_png_sizes([wrong])
      with self.assertRaises(SystemExit):
        branding.check_png_sizes([pathlib.Path(tmp) / 'logo_64.png'])
      not_png = pathlib.Path(tmp) / 'logo_48.png'
      not_png.write_text('nope')
      with self.assertRaises(SystemExit):
        branding.check_png_sizes([not_png])

  def test_icns_follows_chromium_convention(self):
    self.assertEqual(branding.MAC_ICNS_SET,
                     [(16, 1), (32, 1), (128, 1), (256, 1), (512, 1)])
    self.assertLessEqual(set(branding.MAC_ICNS_SET),
                         set(branding.MAC_ICON_SET))


class InfoPlistTest(unittest.TestCase):

  def test_renames_document_kinds(self):
    text = ('<key>UTTypeDescription</key>\n'
            '<string>Chromium Extension</string>\n'
            '<string>Chromium Shortcut</string>\n'
            '<string>org.chromium.extension</string>')
    out = branding.rebrand_info_plist(text)
    self.assertIn('<string>Koan Extension</string>', out)
    self.assertIn('<string>Koan Shortcut</string>', out)
    # Type identifiers stay: they tie document types to the declarations.
    self.assertIn('org.chromium.extension', out)
    self.assertEqual(branding.rebrand_info_plist(out), out)

  def test_owned_by_branding(self):
    self.assertIn(branding.INFO_PLIST_FILE, branding.BRANDED_FILES)


class MacEnvTest(unittest.TestCase):

  def test_requires_home_inside_folder(self):
    env = {'KOAN_MAC': '/w/koan-mac', 'HOME': '/Users/me'}
    with mock.patch.dict(branding.os.environ, env, clear=True):
      with self.assertRaises(SystemExit):
        branding.require_mac_env()
    with mock.patch.dict(branding.os.environ, {'HOME': '/Users/me'},
                         clear=True):
      with self.assertRaises(SystemExit):
        branding.require_mac_env()
    env = {'KOAN_MAC': '/w/koan-mac', 'HOME': '/w/koan-mac/home'}
    with mock.patch.dict(branding.os.environ, env, clear=True):
      branding.require_mac_env()
