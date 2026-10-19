#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Rebrands Chromium's user-visible product name and logos to Koan.

Run through scripts/apply_branding.sh. The edits are bulk replacements, so
they live here instead of in patches/: the script is idempotent and re-runs
cleanly after a Chromium rebase. Files it touches are listed in
BRANDED_FILES so apply_patches.sh export leaves them out of patches/.

Only the product name changes. The Chromium project keeps its credit:
"The Chromium Authors", "the Chromium open source project", the linked
"Chromium" in about-page credits, and ChromiumOS are left alone.
"""

import argparse
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / 'chromium' / 'src'
ART = ROOT / 'branding' / 'out'
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

# chromium/src path -> branding/out artifact.
LOGOS = {
    'chrome/app/theme/chromium/product_logo_16.png': 'logo_16.png',
    'chrome/app/theme/chromium/product_logo_24.png': 'logo_24.png',
    'chrome/app/theme/chromium/product_logo_48.png': 'logo_48.png',
    'chrome/app/theme/chromium/product_logo_64.png': 'logo_64.png',
    'chrome/app/theme/chromium/product_logo_128.png': 'logo_128.png',
    'chrome/app/theme/chromium/product_logo_256.png': 'logo_256.png',
    'chrome/app/theme/chromium/product_logo_22_mono.png': 'logo_22_mono.png',
    'chrome/app/theme/chromium/linux/product_logo_24.png': 'logo_24.png',
    'chrome/app/theme/chromium/linux/product_logo_48.png': 'logo_48.png',
    'chrome/app/theme/chromium/linux/product_logo_64.png': 'logo_64.png',
    'chrome/app/theme/chromium/linux/product_logo_128.png': 'logo_128.png',
    'chrome/app/theme/chromium/linux/product_logo_256.png': 'logo_256.png',
    'chrome/app/theme/chromium/linux/product_logo_32.xpm': 'logo_32.xpm',
    'chrome/app/theme/default_100_percent/chromium/product_logo_16.png':
        'logo_16.png',
    'chrome/app/theme/default_100_percent/chromium/product_logo_32.png':
        'logo_32.png',
    'chrome/app/theme/default_100_percent/chromium/linux/product_logo_16.png':
        'logo_16.png',
    'chrome/app/theme/default_100_percent/chromium/linux/product_logo_32.png':
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

BRANDED_FILES = STRING_FILES + [CR_ICONS_FILE, BRANDING_FILE] + sorted(LOGOS)

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


def save_pristine(rel):
  dst = PRISTINE / rel
  if not dst.exists():
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(SRC / rel, dst)


def apply():
  missing = [a for a in set(LOGOS.values()) if not (ART / a).exists()]
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
  for rel, art in LOGOS.items():
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
  parser.add_argument('command', choices=['apply', 'revert', 'list'])
  args = parser.parse_args()
  if args.command == 'apply':
    apply()
  elif args.command == 'revert':
    revert()
  else:
    print('\n'.join(BRANDED_FILES))


if __name__ == '__main__':
  main()
