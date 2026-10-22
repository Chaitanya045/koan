#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Converts one of Zen's single-colour SVG icons to Chromium's .icon format.

  scripts/svg_to_icon.py <zen-icons/name.svg> > <koan_theme_name>.icon

Handles <path d="..."> elements (all SVG path commands, made absolute) and
the viewBox; the fill rule is SVG's default, nonzero. The output keeps the
icon's license (Zen's icons are MPL-2.0).
"""

import os
import re
import sys

HEADER = """// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Zen's zen-icons/%s, converted by scripts/svg_to_icon.py.
"""

TOKEN = re.compile(r'[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?')
ARGS = {'M': 2, 'L': 2, 'H': 1, 'V': 1, 'C': 6, 'S': 4, 'Q': 4, 'T': 2,
        'A': 7, 'Z': 0}


def fmt(v):
    s = ('%.4f' % v).rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


def tokenize(d):
    out = []
    for m in TOKEN.finditer(d):
        out.append(m.group(0))
    return out


def convert_path(d):
    tokens = tokenize(d)
    lines = []
    i = 0
    cmd = None
    cx = cy = sx = sy = 0.0
    last_ctrl = None  # for S/T reflection
    last_cmd = None
    while i < len(tokens):
        t = tokens[i]
        if re.match(r'[A-Za-z]', t):
            cmd = t
            i += 1
            if cmd in 'Zz':
                lines.append('CLOSE,')
                cx, cy = sx, sy
                last_cmd = 'Z'
                continue
        n = ARGS[cmd.upper()]
        args = []
        while len(args) < n:
            tok = tokens[i]
            if cmd in 'Aa' and len(args) in (3, 4) and len(tok) > 1 and \
                    tok[0] in '01' and not tok.startswith('0.'):
                # Packed arc flags, e.g. "01" or "1.5".
                args.append(float(tok[0]))
                tokens[i] = tok[1:]
                continue
            args.append(float(tok))
            i += 1
        rel = cmd.islower()
        c = cmd.upper()
        if c == 'M':
            x, y = args
            if rel:
                x += cx; y += cy
            lines.append('MOVE_TO, %s, %s,' % (fmt(x), fmt(y)))
            cx, cy, sx, sy = x, y, x, y
            cmd = 'l' if rel else 'L'  # implicit lineto after moveto
            last_ctrl = None
        elif c == 'L':
            x, y = args
            if rel:
                x += cx; y += cy
            lines.append('LINE_TO, %s, %s,' % (fmt(x), fmt(y)))
            cx, cy = x, y
            last_ctrl = None
        elif c == 'H':
            x = args[0] + (cx if rel else 0)
            lines.append('H_LINE_TO, %s,' % fmt(x))
            cx = x
            last_ctrl = None
        elif c == 'V':
            y = args[0] + (cy if rel else 0)
            lines.append('V_LINE_TO, %s,' % fmt(y))
            cy = y
            last_ctrl = None
        elif c in 'CS':
            if c == 'C':
                x1, y1, x2, y2, x, y = args
                if rel:
                    x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy
            else:
                x2, y2, x, y = args
                if rel:
                    x2 += cx; y2 += cy; x += cx; y += cy
                if last_ctrl and last_cmd in 'CS':
                    x1, y1 = 2 * cx - last_ctrl[0], 2 * cy - last_ctrl[1]
                else:
                    x1, y1 = cx, cy
            lines.append('CUBIC_TO, %s,' % ', '.join(
                fmt(v) for v in (x1, y1, x2, y2, x, y)))
            last_ctrl = (x2, y2)
            cx, cy = x, y
        elif c in 'QT':
            if c == 'Q':
                x1, y1, x, y = args
                if rel:
                    x1 += cx; y1 += cy; x += cx; y += cy
            else:
                x, y = args
                if rel:
                    x += cx; y += cy
                if last_ctrl and last_cmd in 'QT':
                    x1, y1 = 2 * cx - last_ctrl[0], 2 * cy - last_ctrl[1]
                else:
                    x1, y1 = cx, cy
            lines.append('QUADRATIC_TO, %s,' % ', '.join(
                fmt(v) for v in (x1, y1, x, y)))
            last_ctrl = (x1, y1)
            cx, cy = x, y
        elif c == 'A':
            rx, ry, rot, large, sweep, x, y = args
            if rel:
                x += cx; y += cy
            lines.append('ARC_TO, %s,' % ', '.join(
                fmt(v) for v in (rx, ry, rot, large, sweep, x, y)))
            cx, cy = x, y
            last_ctrl = None
        last_cmd = c
    return lines


def main():
    svg = open(sys.argv[1]).read()
    view_box = re.search(r'viewBox="([^"]+)"', svg).group(1).split()
    size = int(float(view_box[2]))
    out = [HEADER % os.path.basename(sys.argv[1]),
           'CANVAS_DIMENSIONS, %d,' % size]
    paths = re.findall(r'<path[^>]*\sd="([^"]+)"', svg)
    for n, d in enumerate(paths):
        if n:
            out.append('NEW_PATH,')
        out.append('FILL_RULE_NONZERO,')
        out.extend(convert_path(d))
    print('\n'.join(out))


if __name__ == '__main__':
    main()
