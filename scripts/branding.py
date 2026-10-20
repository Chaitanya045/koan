#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Rebrands Chromium's user-visible product name and logos to Koan.

Run through scripts/apply_branding.sh. The edits are bulk replacements, so
they live here instead of in patches/: the script is idempotent and re-runs
cleanly after a Chromium rebase. Files it touches are listed in
BRANDED_FILES so apply_patches.sh export leaves them out of patches/.

Linux and macOS share the strings, BRANDING and the in-browser logos. Linux
adds its desktop icons; macOS adds the app icon: app.icns (iconutil) and the
asset catalog Assets.car (actool), which Chromium checks in pre-compiled
(docs/mac/icons.md) and copies into the app bundle. The bundle name, display
name and bundle id come from BRANDING (PRODUCT_FULLNAME, MAC_BUNDLE_ID); the
localized Info.plist strings come from the rebranded string files.

Keep this file Python 3.9 compatible: that is macOS's /usr/bin/python3.

Only the product name changes. The Chromium project keeps its credit:
"The Chromium Authors", "the Chromium open source project", the linked
"Chromium" in about-page credits, and ChromiumOS are left alone.
"""

import argparse
import json
import pathlib
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / 'chromium' / 'src'
ART = ROOT / 'branding' / 'out'
MAC_ART = ART / 'mac'
# Icon Composer document for the macOS 26+ app icon (Liquid Glass).
MAC_APP_ICON = ROOT / 'branding' / 'mac' / 'AppIcon.icon'
PRISTINE = ROOT / '.branding-pristine'

STRING_FILES = [
    'chrome/app/chromium_strings.grd',
    'chrome/app/settings_chromium_strings.grdp',
    'components/components_chromium_strings.grd',
]

# WebUI icon set: cr:chrome-product (Settings menu "About", etc.).
CR_ICONS_FILE = 'ui/webui/resources/cr_elements/icons.html.ts'

BRANDING_FILE = 'chrome/app/theme/chromium/BRANDING'
BRANDING = """COMPANY_FULLNAME=The Koan Authors
COMPANY_SHORTNAME=The Koan Authors
PRODUCT_FULLNAME=Koan
PRODUCT_SHORTNAME=Koan
PRODUCT_INSTALLER_FULLNAME=Koan Installer
PRODUCT_INSTALLER_SHORTNAME=Koan Installer
COPYRIGHT=Copyright @LASTCHANGE_YEAR@ The Koan Authors and The Chromium Authors. All rights reserved.
MAC_BUNDLE_ID=dev.koan.Koan
MAC_CREATOR_CODE=Cr24
MAC_TEAM_ID=
"""

# chromium/src path -> branding/out artifact, on every platform.
LOGOS = {
    'chrome/app/theme/chromium/product_logo_16.png': 'logo_16.png',
    'chrome/app/theme/chromium/product_logo_24.png': 'logo_24.png',
    'chrome/app/theme/chromium/product_logo_48.png': 'logo_48.png',
    'chrome/app/theme/chromium/product_logo_64.png': 'logo_64.png',
    'chrome/app/theme/chromium/product_logo_128.png': 'logo_128.png',
    'chrome/app/theme/chromium/product_logo_256.png': 'logo_256.png',
    'chrome/app/theme/chromium/product_logo_22_mono.png': 'logo_22_mono.png',
    'chrome/app/theme/default_100_percent/chromium/product_logo_16.png':
        'logo_16.png',
    'chrome/app/theme/default_100_percent/chromium/product_logo_32.png':
        'logo_32.png',
    'chrome/app/theme/default_200_percent/chromium/product_logo_16.png':
        'logo_32.png',
    'chrome/app/theme/default_200_percent/chromium/product_logo_32.png':
        'logo_64.png',
    'chrome/app/theme/default_100_percent/chromium/product_logo_name_22.png':
        'wordmark_22.png',
    'chrome/app/theme/default_100_percent/chromium/'
    'product_logo_name_22_white.png': 'wordmark_22_white.png',
    'chrome/app/theme/default_200_percent/chromium/product_logo_name_22.png':
        'wordmark_44.png',
    'chrome/app/theme/default_200_percent/chromium/'
    'product_logo_name_22_white.png': 'wordmark_44_white.png',
}

# Linux only: desktop and installer icons.
LINUX_LOGOS = {
    'chrome/app/theme/chromium/linux/product_logo_24.png': 'logo_24.png',
    'chrome/app/theme/chromium/linux/product_logo_48.png': 'logo_48.png',
    'chrome/app/theme/chromium/linux/product_logo_64.png': 'logo_64.png',
    'chrome/app/theme/chromium/linux/product_logo_128.png': 'logo_128.png',
    'chrome/app/theme/chromium/linux/product_logo_256.png': 'logo_256.png',
    'chrome/app/theme/chromium/linux/product_logo_32.xpm': 'logo_32.xpm',
    'chrome/app/theme/default_100_percent/chromium/linux/product_logo_16.png':
        'logo_16.png',
    'chrome/app/theme/default_100_percent/chromium/linux/product_logo_32.png':
        'logo_32.png',
}

# macOS only: the app icon, built by `branding.py mac-assets`.
MAC_LOGOS = {
    'chrome/app/theme/chromium/mac/app.icns': 'mac/app.icns',
    'chrome/app/theme/chromium/mac/Assets.car': 'mac/Assets.car',
}

PLATFORMS = ('linux', 'mac')


def host_platform():
  return 'mac' if sys.platform == 'darwin' else 'linux'


def logos_for(platform):
  """chromium/src path -> artifact for everything branded on `platform`."""
  extra = MAC_LOGOS if platform == 'mac' else LINUX_LOGOS
  return {**LOGOS, **extra}


# Every platform's files: apply_patches.sh export must skip all of them.
BRANDED_FILES = STRING_FILES + [CR_ICONS_FILE, BRANDING_FILE] + sorted(
    {**LOGOS, **LINUX_LOGOS, **MAC_LOGOS})

# The macOS app icon set as (points, scale), the sizes iconutil and actool
# expect for the mac idiom.
MAC_ICON_SET = [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2),
                (256, 1), (256, 2), (512, 1), (512, 2)]

# Apple's macOS icon grid: the icon body is 824 px on a 1024 px canvas, the
# margin leaving room for the shadow. branding/logo.svg's body is the
# rounded square at (8, 8), 240 x 240, in a 256 x 256 view box.
MAC_ICON_BODY_FRACTION = 824 / 1024
LOGO_VIEW_BOX = 'viewBox="0 0 256 256"'
LOGO_BODY = (8, 8, 240)

# "Chromium" as the product name: not ChromiumOS (no word boundary), not the
# project or its authors, not the linked project name in credits.
PRODUCT_NAME = re.compile(
    r'\bChromium\b'
    r'(?! Authors)'
    r'(?! open source)'
    r'(?! project)'
    r'(?!<ph name="END_LINK_CHROMIUM")')

MESSAGE = re.compile(r'(<message\b[^>]*>)(.*?)(</message>)', re.S)


def rebrand_messages(text):
  """Replaces the product name inside <message> bodies only.

  Attributes (name, desc, ...) and everything outside messages stay as they
  are. Changed messages lose their translations (grit falls back to English
  for them); untouched messages keep theirs.
  """

  def fix(match):
    return match.group(1) + PRODUCT_NAME.sub('Koan', match.group(2)) + \
        match.group(3)

  return MESSAGE.sub(fix, text)


def enso_path(size, y_offset=0.0):
  """A filled open ring (the ensō) as an SVG path in a size x size box.

  Same geometry as branding/logo.svg: from -50 degrees the long way round to
  -15 degrees.
  """
  import math
  cx, cy = size / 2, size / 2 + y_offset
  outer, inner = size * 0.40, size * 0.25

  def point(r, deg):
    a = math.radians(deg)
    return f'{cx + r * math.cos(a):.2f} {cy + r * math.sin(a):.2f}'

  return (f'M{point(outer, -50)} A{outer:.2f} {outer:.2f} 0 1 0 '
          f'{point(outer, -15)} L{point(inner, -15)} '
          f'A{inner:.2f} {inner:.2f} 0 1 1 {point(inner, -50)} Z')


def rebrand_cr_icons(text):
  """Replaces the product icon paths in the cr icon set with the ensō."""
  icon = re.compile(
      r'(<g id="chrome-product"( viewBox="0 -960 960 960")?>\s*<path\s+d=")'
      r'[^"]*(")')

  def fix(match):
    big = match.group(2) is not None
    path = enso_path(960, -960) if big else enso_path(24)
    return match.group(1) + path + match.group(3)

  return icon.sub(fix, text)


def mac_icon_pixels():
  """Pixel sizes of the macOS icon bitmaps (16 to 1024), rendered as
  branding/out/mac/appicon_<px>.png by apply_branding.sh."""
  return sorted({points * scale for points, scale in MAC_ICON_SET})


def mac_iconset_name(points, scale):
  """File name iconutil expects in an .iconset for (points, scale)."""
  suffix = '@2x' if scale == 2 else ''
  return f'icon_{points}x{points}{suffix}.png'


def mac_icon_svg(text):
  """branding/logo.svg on the macOS icon grid: the same drawing with a wider
  view box, so the rounded square is 824/1024 of the canvas, centred."""
  x, y, size = LOGO_BODY
  canvas = size / MAC_ICON_BODY_FRACTION
  left = x + size / 2 - canvas / 2
  top = y + size / 2 - canvas / 2
  view_box = f'viewBox="{left:.3f} {top:.3f} {canvas:.3f} {canvas:.3f}"'
  if text.count(LOGO_VIEW_BOX) != 1:
    raise ValueError(f'logo.svg must have exactly one {LOGO_VIEW_BOX}')
  return text.replace(LOGO_VIEW_BOX, view_box)


def appiconset_contents():
  """Contents.json of the AppIcon.appiconset in Koan's asset catalog."""
  return {
      'images': [{
          'filename': f'appicon_{points * scale}.png',
          'idiom': 'mac',
          'scale': f'{scale}x',
          'size': f'{points}x{points}',
      } for points, scale in MAC_ICON_SET],
      'info': {
          'author': 'xcode',
          'version': 1
      },
  }


def mac_deployment_target():
  """mac_deployment_target from Chromium's build/config/mac/mac_sdk.gni."""
  gni = (SRC / 'build' / 'config' / 'mac' / 'mac_sdk.gni').read_text()
  match = re.search(r'^\s*mac_deployment_target\s*=\s*"([^"]+)"', gni, re.M)
  if not match:
    sys.exit('mac_deployment_target not found in mac_sdk.gni')
  return match.group(1)


def actool_command(catalog, out, app_icon=None):
  """actool invocation compiling `catalog` (and the Icon Composer
  `app_icon`, if given) into out/Assets.car, as Chromium's
  tools/mac/icons/compile_car.py does."""
  command = [
      'xcrun', 'actool', '--output-format=xml1', '--notices', '--warnings',
      '--errors', '--platform=macosx', '--target-device=mac',
      '--lightweight-asset-runtime-mode=enabled', '--app-icon=AppIcon',
      f'--minimum-deployment-target={mac_deployment_target()}',
      f'--output-partial-info-plist={out / "partial.plist"}',
      f'--compile={out}'
  ]
  if app_icon:
    # Keep the bitmaps in AppIcon.appiconset for macOS before 26 instead of
    # generating them from the .icon (undocumented; see compile_car.py).
    command += [
        '--enable-icon-stack-fallback-generation=disabled',
        '--include-all-app-icons'
    ]
  command.append(str(catalog))
  if app_icon:
    command.append(str(app_icon))
  return command


def actool_problems(stdout):
  """Errors and warnings from actool's xml1 output, as strings."""
  try:
    result = plistlib.loads(stdout)
  except Exception:  # Not a plist: actool failed before reporting.
    return [stdout.decode(errors='replace').strip()]
  problems = []
  for key, value in sorted(result.items()):
    if key.endswith(('errors', 'warnings')) and value:
      problems += [f'{key}: {item}' for item in value]
  return problems


def write_asset_catalog(catalog):
  """Koan's Assets.xcassets: the app icon bitmaps, and the "Icon" image that
  Info.plist's UTTypeIconBadgeName uses to badge document icons."""
  catalog.mkdir(parents=True)
  info = {'info': {'author': 'xcode', 'version': 1}}
  (catalog / 'Contents.json').write_text(json.dumps(info, indent=2))
  appiconset = catalog / 'AppIcon.appiconset'
  appiconset.mkdir()
  for px in mac_icon_pixels():
    shutil.copyfile(MAC_ART / f'appicon_{px}.png',
                    appiconset / f'appicon_{px}.png')
  (appiconset / 'Contents.json').write_text(
      json.dumps(appiconset_contents(), indent=2))
  badge = catalog / 'Icon.iconset'
  badge.mkdir()
  shutil.copyfile(ART / 'logo_256.png', badge / 'icon_256x256.png')
  shutil.copyfile(ART / 'logo_512.png', badge / 'icon_256x256@2x.png')


def compile_asset_catalog(catalog, out):
  """Runs actool; returns out/Assets.car. Tries the Icon Composer icon
  first, and falls back to the bitmaps alone if actool rejects it (macOS
  26+ then shows the bitmap icon)."""
  for app_icon in (MAC_APP_ICON, None):
    if out.exists():
      shutil.rmtree(out)
    out.mkdir()
    run = subprocess.run(actool_command(catalog, out, app_icon),
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE,
                         check=False)
    problems = actool_problems(run.stdout)
    for problem in problems:
      print(f'actool: {problem}', file=sys.stderr)
    if run.returncode == 0 and (out / 'Assets.car').exists():
      return out / 'Assets.car'
    if app_icon:
      print(f'actool rejected {app_icon.name}; using the bitmap icon only',
            file=sys.stderr)
  sys.exit('actool failed to compile the asset catalog: '
           + run.stderr.decode(errors='replace'))


def mac_assets():
  """macOS: builds branding/out/mac/app.icns and Assets.car from the icon
  bitmaps apply_branding.sh rendered."""
  needed = [MAC_ART / f'appicon_{px}.png' for px in mac_icon_pixels()]
  needed += [ART / 'logo_256.png', ART / 'logo_512.png']
  missing = [str(path) for path in needed if not path.exists()]
  if missing:
    sys.exit(f'missing icon bitmaps {missing} '
             '(run scripts/apply_branding.sh, which renders them)')
  with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    iconset = tmp / 'Koan.iconset'
    iconset.mkdir()
    for points, scale in MAC_ICON_SET:
      shutil.copyfile(MAC_ART / f'appicon_{points * scale}.png',
                      iconset / mac_iconset_name(points, scale))
    subprocess.run(
        ['iconutil', '-c', 'icns', '-o',
         str(MAC_ART / 'app.icns'),
         str(iconset)],
        check=True)
    print(f'built      {MAC_ART / "app.icns"}')
    catalog = tmp / 'Assets.xcassets'
    write_asset_catalog(catalog)
    car = compile_asset_catalog(catalog, tmp / 'compiled')
    shutil.copyfile(car, MAC_ART / 'Assets.car')
    print(f'built      {MAC_ART / "Assets.car"}')


def save_pristine(rel):
  dst = PRISTINE / rel
  if not dst.exists():
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(SRC / rel, dst)


def apply(platform):
  logos = logos_for(platform)
  missing = sorted(a for a in set(logos.values()) if not (ART / a).exists())
  if missing:
    sys.exit(f'missing branding artifacts in {ART}: {missing} '
             '(run scripts/apply_branding.sh, which renders them)')
  changed = 0
  for rel in STRING_FILES:
    save_pristine(rel)
    path = SRC / rel
    old = path.read_text()
    new = rebrand_messages(old)
    if new != old:
      path.write_text(new)
      changed += 1
      print(f'rebranded  {rel}')
  save_pristine(CR_ICONS_FILE)
  old = (SRC / CR_ICONS_FILE).read_text()
  new = rebrand_cr_icons(old)
  if new != old:
    (SRC / CR_ICONS_FILE).write_text(new)
    changed += 1
    print(f'rebranded  {CR_ICONS_FILE}')
  save_pristine(BRANDING_FILE)
  if (SRC / BRANDING_FILE).read_text() != BRANDING:
    (SRC / BRANDING_FILE).write_text(BRANDING)
    changed += 1
    print(f'rebranded  {BRANDING_FILE}')
  for rel, art in logos.items():
    save_pristine(rel)
    if (SRC / rel).read_bytes() != (ART / art).read_bytes():
      shutil.copyfile(ART / art, SRC / rel)
      changed += 1
      print(f'logo       {rel}')
  print(f'{changed} file(s) changed' if changed else 'already branded')


def revert():
  for rel in BRANDED_FILES:
    src = PRISTINE / rel
    if src.exists():
      shutil.copy2(src, SRC / rel)
      print(f'reverted   {rel}')
  shutil.rmtree(PRISTINE, ignore_errors=True)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('command',
                      choices=[
                          'apply', 'revert', 'list', 'mac-assets',
                          'mac-icon-sizes', 'mac-icon-svg'
                      ])
  parser.add_argument('svg', nargs='?', help='mac-icon-svg: the logo SVG')
  parser.add_argument('--platform',
                      choices=PLATFORMS,
                      default=host_platform(),
                      help='apply: which platform to brand (default: host)')
  args = parser.parse_args()
  if args.command == 'apply':
    apply(args.platform)
  elif args.command == 'revert':
    revert()
  elif args.command == 'list':
    print('\n'.join(BRANDED_FILES))
  elif args.command == 'mac-assets':
    mac_assets()
  elif args.command == 'mac-icon-sizes':
    print(' '.join(str(px) for px in mac_icon_pixels()))
  else:
    if not args.svg:
      parser.error('mac-icon-svg needs the logo SVG')
    sys.stdout.write(mac_icon_svg(pathlib.Path(args.svg).read_text()))


if __name__ == '__main__':
  main()
