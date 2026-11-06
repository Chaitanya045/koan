#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Checks that every Chromium icon in koan_icons.cc's table exists on a platform.

  scripts/check_icon_table.py [--platform mac|linux] [<chromium/src>]

Chromium's icon targets list some icons per platform (omnibox's `send.icon`
only on Linux, `ios_share.icon` only on macOS): a Linux build's generated
headers have symbols the Mac's lack. This reads the aggregate_vector_icons
lists in the BUILD.gn files (what the platform's `is_*` conditions leave) and
the table's `#if BUILDFLAG(IS_MAC)` / `IS_LINUX` blocks, and reports table
entries with no icon file. Default platform: mac (Koan's product); default
checkout: chromium/src of this repo.
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = [  # (namespace in the table, BUILD.gn, target)
    ('vector_icons', 'components/vector_icons/BUILD.gn', 'components_vector_icons'),
    ('', 'chrome/app/vector_icons/BUILD.gn', 'chrome_vector_icons'),
    ('views', 'ui/views/BUILD.gn', 'views_vector_icons'),
    ('omnibox', 'components/omnibox/browser/BUILD.gn', 'omnibox_vector_icons'),
]
PLATFORM_FLAGS = {'mac': {'is_mac', 'is_apple', 'is_posix'},
                  'linux': {'is_linux', 'is_posix'}}
ALL_FLAGS = {'is_mac', 'is_apple', 'is_posix', 'is_linux', 'is_win', 'is_chromeos',
             'is_android', 'is_fuchsia', 'is_chrome_branded'}


def symbol(icon_file):
    return 'k' + ''.join(w.capitalize() for w in icon_file.split('_')) + 'Icon'


def icons(path, target, platform):
    text = open(path).read()
    start = text.index('aggregate_vector_icons("%s")' % target)
    begin = text.index('{', start)
    depth = 0
    for end in range(begin, len(text)):
        depth += {'{': 1, '}': -1}.get(text[end], 0)
        if depth == 0:
            break
    out = set()
    active = []  # one entry per open `if`: does the platform keep it?
    for line in text[begin:end].split('\n'):
        cond = re.match(r'\s*if \((.*)\) \{', line)
        if cond:
            flags = set(re.findall(r'\w+', cond.group(1))) & ALL_FLAGS
            active.append(flags <= PLATFORM_FLAGS[platform])
        elif re.match(r'\s*\}\s*$', line) and active:
            active.pop()
        elif all(active):
            out |= {f.split('/')[-1] for f in re.findall(r'"([\w/]+)\.icon"', line)}
    return {symbol(f) for f in out}


def table(platform):
    path = os.path.join(ROOT, 'src/browser/ui/vector_icons/koan_icons.cc')
    keep = [True]
    for line in open(path):
        if line.startswith('#if'):
            keep.append(platform.upper() in line)
        elif line.startswith('#elif'):
            keep[-1] = platform.upper() in line
        elif line.startswith('#endif'):
            keep.pop()
        elif all(keep):
            m = re.match(r'\s*\{&(?:(\w+)::)?(k\w+Icon), &kKoan\w+Icon\},', line)
            if m:
                yield m.group(1) or '', m.group(2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--platform', default='mac', choices=PLATFORM_FLAGS)
    ap.add_argument('src', nargs='?', default=os.path.join(ROOT, 'chromium/src'))
    args = ap.parse_args()
    have = {(ns, s) for ns, build, target in TARGETS
            for s in icons(os.path.join(args.src, build), target, args.platform)}
    missing = [(ns, s) for ns, s in table(args.platform) if (ns, s) not in have]
    for ns, s in missing:
        print('no such icon on %s: %s%s' % (args.platform, ns + '::' if ns else '', s))
    print('%d table entries checked on %s, %d missing' %
          (sum(1 for _ in table(args.platform)), args.platform, len(missing)))
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
