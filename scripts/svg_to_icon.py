#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Converts a single-colour SVG icon to Chromium's .icon vector format.

  scripts/svg_to_icon.py [--fill] [--source TEXT] <icon.svg> > <koan_name.icon>

Reads Lucide / Tabler style stroked icons (<path>, <circle>, <ellipse>,
<rect rx>, <line>, <polyline>, <polygon>; stroke and fill inherited from the
<svg> element) as well as plain filled icons. Every stroke is turned into
an outline with Skia's own stroker (round caps and joins when the SVG asks
for them), and all shapes are unioned into one non-zero path. The result
paints identically at any size and does not depend on STROKE commands
(Chromium's stroker can't do round joins).

--fill also fills closed shapes (for the "-fill" variants of an icon).
--ink WxH fits the glyph to that outer size (canvas units, strokes included),
each axis on its own; --scale sets the fit to Zen's glyph size (default ZEN_FIT); an icon that would
then come within EDGE_PAD of the canvas edge is shrunk, strokes with it, to fit.
--source puts a line into the header comment (set, licence).

Dev-time tool; needs `pip install skia-pathops fonttools`. The generated
.icon files are committed, builds never run this.
"""

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET

import pathops
from fontTools.pens.basePen import BasePen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import parse_path

CAPS = {'round': pathops.LineCap.ROUND_CAP, 'square': pathops.LineCap.SQUARE_CAP,
        'butt': pathops.LineCap.BUTT_CAP}
JOINS = {'round': pathops.LineJoin.ROUND_JOIN, 'bevel': pathops.LineJoin.BEVEL_JOIN,
         'miter': pathops.LineJoin.MITER_JOIN}
SVG = '{http://www.w3.org/2000/svg}'
INHERITED = ('fill', 'fill-rule', 'stroke', 'stroke-width', 'stroke-linecap',
             'stroke-linejoin')
# Elements that draw nothing themselves.
NOT_DRAWN = ('defs', 'clipPath', 'style', 'mask')


class IconPen(BasePen):
    """Collects a path as Chromium .icon commands."""

    def __init__(self):
        super().__init__(None)
        self.lines = []
        self.lo = float('inf')
        self.hi = float('-inf')

    @staticmethod
    def f(v):
        s = ('%.3f' % v).rstrip('0').rstrip('.')
        return '0' if s in ('-0', '') else s

    def _pts(self, *pts):
        # Control points count: they are what the icon file holds and what
        # the checks bound.
        for p in pts:
            self.lo = min(self.lo, *p)
            self.hi = max(self.hi, *p)
        return ', '.join(self.f(c) for p in pts for c in p)

    def _moveTo(self, p):
        self.lines.append('MOVE_TO, %s,' % self._pts(p))

    def _lineTo(self, p):
        self.lines.append('LINE_TO, %s,' % self._pts(p))

    def _curveToOne(self, p1, p2, p3):
        self.lines.append('CUBIC_TO, %s,' % self._pts(p1, p2, p3))

    def _qCurveToOne(self, p1, p2):
        self.lines.append('QUADRATIC_TO, %s,' % self._pts(p1, p2))

    def _closePath(self):
        self.lines.append('CLOSE,')

    def _endPath(self):
        pass


def num(el, name, default=0.0):
    return float(el.get(name, default))


# Zen's (Nucleo) glyphs fill about 91% of their 18-unit box; Lucide's fill 83%
# of 24. The geometry is scaled about the centre by this (strokes keep their
# width) so that the icons are as big as Zen's at the same size.
ZEN_FIT = 1.1
# What the outline (strokes included) may not come closer to the canvas edge
# than, in canvas units (anti-aliasing needs a margin; 0.5 is 0.35 px at 17 pt).
EDGE_PAD = 0.5


def shape_path(el, scale=1.0, size=24.0, origin=(0.0, 0.0)):
    """The geometry of one SVG element as a pathops.Path, or None. `origin` is
    the viewBox's, which moves to the canvas' 0, 0."""
    tag = el.tag.replace(SVG, '')
    path = pathops.Path()
    c = size / 2 * (1 - scale)
    # An element's own translate(x[, y]) is the only transform handled.
    move = re.fullmatch(r'translate\(\s*([-\d.]+)[\s,]*([-\d.]*)\s*\)',
                        el.get('transform', 'translate(0)'))
    dx = float(move.group(1))
    dy = float(move.group(2) or 0)
    pen = TransformPen(path.getPen(), (scale, 0, 0, scale,
                                       c + scale * (dx - origin[0]),
                                       c + scale * (dy - origin[1])))
    if tag == 'path':
        parse_path(el.get('d'), pen)
    elif tag in ('circle', 'ellipse'):
        cx, cy = num(el, 'cx'), num(el, 'cy')
        rx = num(el, 'r') if tag == 'circle' else num(el, 'rx')
        ry = rx if tag == 'circle' else num(el, 'ry')
        # Four cubics, the standard circle approximation.
        k = 0.5522847498
        pen.moveTo((cx + rx, cy))
        pen.curveTo((cx + rx, cy + k * ry), (cx + k * rx, cy + ry), (cx, cy + ry))
        pen.curveTo((cx - k * rx, cy + ry), (cx - rx, cy + k * ry), (cx - rx, cy))
        pen.curveTo((cx - rx, cy - k * ry), (cx - k * rx, cy - ry), (cx, cy - ry))
        pen.curveTo((cx + k * rx, cy - ry), (cx + rx, cy - k * ry), (cx + rx, cy))
        pen.closePath()
    elif tag == 'rect':
        x, y, w, h = (num(el, n) for n in ('x', 'y', 'width', 'height'))
        rx = num(el, 'rx', el.get('ry', 0))
        ry = num(el, 'ry', rx)
        rx, ry = min(rx, w / 2), min(ry, h / 2)
        if not rx and not ry:
            parse_path('M%g %gh%gv%gh%gz' % (x, y, w, h, -w), pen)
        else:
            parse_path(
                'M%g %gh%ga%g %g 0 0 1 %g %gv%ga%g %g 0 0 1 %g %gh%ga%g %g 0 0 1 '
                '%g %gv%ga%g %g 0 0 1 %g %gz' %
                (x + rx, y, w - 2 * rx, rx, ry, rx, ry, h - 2 * ry, rx, ry, -rx,
                 ry, -(w - 2 * rx), rx, ry, -rx, -ry, -(h - 2 * ry), rx, ry, rx,
                 -ry), pen)
    elif tag == 'line':
        parse_path('M%g %gL%g %g' % tuple(
            num(el, n) for n in ('x1', 'y1', 'x2', 'y2')), pen)
    elif tag in ('polyline', 'polygon'):
        pts = el.get('points').replace(',', ' ').split()
        d = 'M' + ' L'.join('%s %s' % (pts[i], pts[i + 1])
                            for i in range(0, len(pts), 2))
        parse_path(d + ('z' if tag == 'polygon' else ''), pen)
    else:
        return None
    return path


def collect(el, style, shapes, geometry, clips, clip=None):
    """Appends (path, style) for `el` and its children. `geometry` is
    (scale, size, origin) for shape_path, `clips` the clipPath elements by id;
    a clipPath of rects clips what it is applied to."""
    if el.tag.replace(SVG, '') in NOT_DRAWN:
        return
    style = dict(style)
    for k in INHERITED:
        if el.get(k) is not None:
            style[k] = el.get(k)
    ref = el.get('clip-path')
    if ref:
        rects = [shape_path(r, *geometry) for r in clips[ref[len('url(#'):-1]]]
        clip = rects[0]
        for r in rects[1:]:
            clip = pathops.op(clip, r, pathops.PathOp.UNION)
    path = shape_path(el, *geometry)
    if path is not None:
        if clip is not None:
            path.fillType = pathops.FillType.WINDING
            path = pathops.op(path, clip, pathops.PathOp.INTERSECTION)
        shapes.append((path, style))
    for child in el:
        collect(child, style, shapes, geometry, clips, clip)


def fit_to_canvas(path, size):
    """Shrinks `path` about the canvas centre, stroke width included, until it
    has all its points (control points included) EDGE_PAD inside the canvas (otherwise the image clips it)."""
    c = size / 2
    pen = IconPen()
    path.draw(pen)
    reach = max(c - pen.lo, pen.hi - c)
    f = min(1.0, (c - EDGE_PAD) / reach)
    if f == 1.0:
        return path
    fitted = pathops.Path()
    path.draw(TransformPen(fitted.getPen(), (f, 0, 0, f, c * (1 - f), c * (1 - f))))
    fitted.fillType = pathops.FillType.WINDING
    return fitted


def fit_ink(shapes, ink, size):
    """Scales the shapes' geometry about the canvas centre, each axis on its own,
    so that geometry plus the widest stroke measures ink = (width, height) in
    canvas units: icons whose box in Zen has another aspect than Lucide's."""
    boxes = [path.bounds for path, _ in shapes]
    x0, y0 = min(b[0] for b in boxes), min(b[1] for b in boxes)
    x1, y1 = max(b[2] for b in boxes), max(b[3] for b in boxes)
    stroke = max([float(st['stroke-width']) for _, st in shapes
                  if st['stroke'] != 'none'] or [0.0])
    sx = (ink[0] - stroke) / (x1 - x0)
    sy = (ink[1] - stroke) / (y1 - y0)
    c = size / 2
    matrix = (sx, 0, 0, sy, c - sx * (x0 + x1) / 2, c - sy * (y0 + y1) / 2)
    out = []
    for path, st in shapes:
        moved = pathops.Path()
        path.draw(TransformPen(moved.getPen(), matrix))
        moved.fillType = pathops.FillType.WINDING
        out.append((moved, st))
    return out


def convert(svg_text, force_fill=False, scale=ZEN_FIT, fit=True, alpha=None,
            ink=None):
    root = ET.fromstring(svg_text)
    view_box = [float(v) for v in root.get('viewBox').split()]
    assert view_box[2] == view_box[3]
    clips = {e.get('id'): list(e) for e in root.iter(SVG + 'clipPath')}
    shapes = []
    collect(root, {'fill': 'black', 'stroke': 'none', 'stroke-width': '1'},
            shapes, (scale, view_box[2], (view_box[0], view_box[1])), clips)
    if ink:
        shapes = fit_ink(shapes, ink, view_box[2])
    parts = []
    for path, style in shapes:
        fill = style['fill'] != 'none' or force_fill
        stroke = style['stroke'] != 'none'
        if fill:
            filled = pathops.Path(path)
            if style.get('fill-rule') == 'evenodd':
                filled.fillType = pathops.FillType.EVEN_ODD
                filled = pathops.simplify(filled, fix_winding=True)
            filled.fillType = pathops.FillType.WINDING
            parts.append(filled)
        if stroke:
            stroked = pathops.Path(path)
            stroked.stroke(float(style['stroke-width']),
                           CAPS[style.get('stroke-linecap', 'butt')],
                           JOINS[style.get('stroke-linejoin', 'miter')], 4)
            stroked.convertConicsToQuads(0.005)
            stroked.fillType = pathops.FillType.WINDING
            parts.append(stroked)
    result = parts[0]
    for part in parts[1:]:
        result = pathops.op(result, part, pathops.PathOp.UNION)
    result = pathops.simplify(result, fix_winding=True)
    if fit:
        result = fit_to_canvas(result, view_box[2])
    pen = IconPen()
    result.draw(pen)
    lines = pen.lines
    if alpha is not None:
        # The icon's own opacity (the caller's colour keeps its hue).
        lines = ['PATH_COLOR_ALPHA, %d,' % round(alpha * 255)] + lines
    return int(view_box[2]), lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('svg')
    ap.add_argument('--fill', action='store_true')
    ap.add_argument('--scale', type=float, default=ZEN_FIT)
    ap.add_argument('--source', default='')
    args = ap.parse_args()
    size, lines = convert(open(args.svg).read(), args.fill, args.scale)
    out = ['// Copyright 2026 The Koan Authors',
           '// Use of this source code is governed by the MIT license that can be',
           '// found in the LICENSE file.',
           '//',
           '// Generated by scripts/svg_to_icon.py from %s.' %
           os.path.basename(args.svg)]
    if args.source:
        out.append('// %s' % args.source)
    out += ['', 'CANVAS_DIMENSIONS, %d,' % size, 'FILL_RULE_NONZERO,']
    out += lines
    print('\n'.join(out))


if __name__ == '__main__':
    sys.exit(main())
