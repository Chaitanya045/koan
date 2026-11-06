#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Pixel check for Koan's generated icons.

  scripts/icon_check.py [--sheet out.png] [name ...]

Checks every src/browser/ui/vector_icons/koan_*.icon is current, then rasterises it (a small .icon
interpreter on cairo, non-zero fill like Chromium's) and the vendored SVG it
came from (rsvg-convert) at 16, 17, 18 and 20 pt, 1x and 2x, and reports the
per-icon worst mean alpha difference. --sheet writes a contact sheet of the
.icon renders at 17 pt 2x next to the SVG's. Needs pycairo, Pillow and
rsvg-convert.
"""

import argparse
import glob
import io
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cairo
from PIL import Image, ImageChops, ImageStat

import svg_to_icon

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_DIR = os.path.join(ROOT, 'src', 'browser', 'ui', 'vector_icons')
SIZES = (16, 17, 18, 20)
ARGS = {'PATH_COLOR_ALPHA': 1, 'MOVE_TO': 2, 'LINE_TO': 2, 'CUBIC_TO': 6, 'QUADRATIC_TO': 4}


def icon_text(svg, filled, scale, fit=True, alpha=None):
    size, lines = svg_to_icon.convert(open(svg).read(), filled, scale, fit, alpha)
    return 'CANVAS_DIMENSIONS, %d,\nFILL_RULE_NONZERO,\n' % size + '\n'.join(lines)


def points(text):
    """Every coordinate of an .icon file's path commands (control points too)."""
    toks = [t for t in re.split(r'[\s,]+', re.sub(r'//.*', '', text)) if t]
    out = []
    i = 0
    while i < len(toks):
        t = toks[i]
        i += 1
        if t == 'PATH_COLOR_ALPHA':
            i += 1
        elif t in ARGS:
            out += [float(v) for v in toks[i:i + ARGS[t]]]
            i += ARGS[t]
        elif t == 'CANVAS_DIMENSIONS':
            i += 1
    return out


def render_icon(text, px, margin=0):
    """Rasterises an .icon file at px x px, with `margin` more pixels on every
    side that the icon may draw into (to see what the canvas clips)."""
    text = re.sub(r'//.*', '', text)
    toks = [t for t in re.split(r'[\s,]+', text) if t]
    canvas = 24
    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, px + 2 * margin,
                              px + 2 * margin)
    ctx = cairo.Context(surf)
    ctx.translate(margin, margin)
    i = 0
    cur = (0, 0)
    while i < len(toks):
        t = toks[i]
        i += 1
        if t == 'CANVAS_DIMENSIONS':
            canvas = float(toks[i]); i += 1
            ctx.scale(px / canvas, px / canvas)
            ctx.set_fill_rule(cairo.FILL_RULE_EVEN_ODD)
        elif t == 'FILL_RULE_NONZERO':
            ctx.set_fill_rule(cairo.FILL_RULE_WINDING)
        elif t == 'CLOSE':
            ctx.close_path()
        elif t == 'NEW_PATH':
            ctx.fill(); ctx.new_path()
        elif t == 'PATH_COLOR_ALPHA':
            i += 1  # The preview is opaque, like the SVG it is compared with.
        elif t in ARGS:
            a = [float(v) for v in toks[i:i + ARGS[t]]]
            i += ARGS[t]
            if t == 'MOVE_TO':
                ctx.move_to(*a); cur = tuple(a)
            elif t == 'LINE_TO':
                ctx.line_to(*a); cur = tuple(a[-2:])
            elif t == 'CUBIC_TO':
                ctx.curve_to(*a); cur = tuple(a[-2:])
            else:
                (x0, y0), (x1, y1), (x2, y2) = cur, a[:2], a[2:]
                ctx.curve_to(x0 + 2 / 3 * (x1 - x0), y0 + 2 / 3 * (y1 - y0),
                             x2 + 2 / 3 * (x1 - x2), y2 + 2 / 3 * (y1 - y2),
                             x2, y2)
                cur = (x2, y2)
        else:
            sys.exit('unsupported command %s' % t)
    ctx.fill()
    buf = io.BytesIO()
    surf.write_to_png(buf)
    return Image.open(buf).convert('RGBA')


def render_svg(path, px):
    svg = open(path).read().replace('currentColor', 'black')
    out = subprocess.run(['rsvg-convert', '-w', str(px), '-h', str(px)],
                         input=svg.encode(), capture_output=True, check=True)
    return Image.open(io.BytesIO(out.stdout)).convert('RGBA')


def alpha_diff(a, b):
    d = ImageChops.difference(a.split()[3], b.split()[3])
    return ImageStat.Stat(d).mean[0] / 255.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sheet')
    ap.add_argument('names', nargs='*')
    args = ap.parse_args()
    # name -> (icon path, svg path, filled)
    rows = []
    for line in open(os.path.join(ROOT, 'third_party', 'icons', 'mapping.txt')):
        p = line.split()
        if not p or p[0].startswith('#'):
            continue
        if args.names and p[0] not in args.names:
            continue
        scale = svg_to_icon.ZEN_FIT
        alpha = None
        for option in p[3:]:
            if option.startswith('scale='):
                scale = float(option[len('scale='):])
            if option.startswith('alpha='):
                alpha = float(option[len('alpha='):])
        rows.append((p[0], os.path.join(ICON_DIR, 'koan_%s.icon' % p[0].replace('-', '_').lower()),
                     os.path.join(ROOT, 'third_party', 'icons', p[1], p[2] + '.svg'),
                     'fill' in p[3:], scale, alpha))
    worst = []
    stale = [zen for zen, icon, svg, filled, scale, alpha in rows
             if re.sub(r'//.*', '', open(icon).read()).split() !=
             icon_text(svg, filled, scale, alpha=alpha).split()]
    if stale:
        sys.exit('stale .icon files, rerun gen_koan_icons.py: %s' % stale)
    clipped = []
    for zen, icon, svg, filled, scale, alpha in rows:
        # Nothing may reach the canvas edge: every point of the outline is
        # inside it, and rendering with a margin (what the canvas would clip)
        # draws nothing there.
        text = open(icon).read()
        pts = points(text)
        canvas = float(re.search(r'CANVAS_DIMENSIONS,\s*([\d.]+)', text).group(1))
        if min(pts) < 0 or max(pts) > canvas:
            clipped.append('%s (%.2f..%.2f)' % (zen, min(pts), max(pts)))
            continue
        for s in SIZES:
            big = render_icon(text, s * 2, s * 2)
            inside = big.crop((s * 2, s * 2, s * 4, s * 4))
            outside = ImageChops.difference(
                big.split()[3],
                Image.new('L', big.size, 0).copy())
            outside.paste(0, (s * 2, s * 2, s * 4, s * 4))
            if outside.getbbox() or inside.getbbox() is None:
                clipped.append('%s at %d pt' % (zen, s))
                break
    if clipped:
        sys.exit('icons reaching past the canvas: %s' % ', '.join(clipped))
    for zen, icon, svg, filled, scale, alpha in rows:
        if filled:
            continue  # The SVG has no fill to compare against.
        # Converter fidelity: the unscaled .icon against the SVG itself.
        text = icon_text(svg, filled, 1.0, fit=False)
        w = max(alpha_diff(render_icon(text, s * k), render_svg(svg, s * k))
                for s in SIZES for k in (1, 2))
        worst.append((w, zen))
    worst.sort(reverse=True)
    for w, zen in worst[:10]:
        print('%.4f %s' % (w, zen))
    print('%d icons compared, worst mean alpha difference %.4f' %
          (len(worst), worst[0][0]))
    if args.sheet:
        cell, cols = 40, 16
        sheet_rows = (len(rows) + cols - 1) // cols
        sheet = Image.new('RGBA', (cols * cell, sheet_rows * cell * 2), 'white')
        for n, (zen, icon, svg, filled, scale, alpha) in enumerate(rows):
            x, y = (n % cols) * cell, (n // cols) * cell * 2
            sheet.alpha_composite(render_icon(open(icon).read(), 34), (x + 3, y + 3))
            if not filled:
                sheet.alpha_composite(render_svg(svg, 34), (x + 3, y + cell + 3))
        sheet.convert('RGB').save(args.sheet)


if __name__ == '__main__':
    main()
