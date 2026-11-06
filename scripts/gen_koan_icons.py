#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Regenerates Koan's Zen-look icon set from third_party/icons.

  scripts/gen_koan_icons.py --lucide <lucide-static>/icons   # vendor + convert
  scripts/gen_koan_icons.py                                  # convert only

third_party/icons/mapping.txt says which free icon (set, name, fill?) stands
for each of Zen's icon names. With --lucide the used SVGs are copied from a
lucide-static checkout into third_party/icons/lucide/ (only those). Every
vendored SVG becomes src/browser/ui/vector_icons/koan_<zen_name>.icon, which
the build turns into koan::kKoan<ZenName>Icon. Prints the BUILD.gn source
list. Needs `pip install skia-pathops fonttools`.
"""

import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svg_to_icon  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS = os.path.join(ROOT, 'third_party', 'icons')
OUT = os.path.join(ROOT, 'src', 'browser', 'ui', 'vector_icons')
SOURCES = {'lucide': 'Lucide (ISC), lucide.dev', 'zen': 'Zen Browser (MPL-2.0)'}
MPL = ['// This Source Code Form is subject to the terms of the Mozilla Public',
       '// License, v. 2.0. If a copy of the MPL was not distributed with this',
       '// file, You can obtain one at http://mozilla.org/MPL/2.0/.']


def read_mapping():
    rows = []
    for line in open(os.path.join(ICONS, 'mapping.txt')):
        line = line.strip()
        if line and not line.startswith('#'):
            parts = line.split()
            scale = svg_to_icon.ZEN_FIT
            for option in parts[3:]:
                if option.startswith('scale='):
                    scale = float(option[len('scale='):])
            rows.append((parts[0], parts[1], parts[2], 'fill' in parts[3:],
                         scale))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lucide')
    args = ap.parse_args()
    names = []
    for zen, set_name, icon, fill, scale in read_mapping():
        dest = os.path.join(ICONS, set_name, icon + '.svg')
        if args.lucide and set_name == 'lucide':
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copy(os.path.join(args.lucide, icon + '.svg'), dest)
        size, lines = svg_to_icon.convert(open(dest).read(), fill, scale)
        if set_name == 'zen':
            header = MPL + ['//',
                            "// Zen's zen-icons/common/library/%s.svg (its resting state, "
                            'third_party/icons/zen),' % icon,
                            '// converted by scripts/gen_koan_icons.py. See THIRD_PARTY_NOTICES.md.',
                            '']
        else:
            header = ['// Copyright 2026 The Koan Authors',
                      '// Use of this source code is governed by the MIT license that can be',
                      '// found in the LICENSE file.',
                      '//',
                      '// Koan\'s stand-in for Zen\'s %s.svg: %s icon "%s"%s, converted by'
                      % (zen, SOURCES[set_name], icon, ' (filled)' if fill else ''),
                      '// scripts/gen_koan_icons.py. See third_party/icons and THIRD_PARTY_NOTICES.md.',
                      '']
        header += ['CANVAS_DIMENSIONS, %d,' % size, 'FILL_RULE_NONZERO,']
        fname = 'koan_%s.icon' % zen.replace('-', '_').lower()
        with open(os.path.join(OUT, fname), 'w') as f:
            f.write('\n'.join(header + lines) + '\n')
        names.append(fname)
    for n in sorted(set(names)):
        print('    "%s",' % n)


if __name__ == '__main__':
    main()
