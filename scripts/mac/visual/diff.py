#!/usr/bin/env python3
"""Perceptual diff of Koan's window crops against reference images.

    diff.py --shots DIR [--ref DIR] [--out DIR] [--threshold PCT] [--record]

For each <state>.png + <state>.json that scripts/mac/visual/capture.mjs left in
--shots, crops every named region (the JSON's rectangles are in points; the
screenshot's scale is its width over the root view's width) and compares it
with <ref>/<state>-<region>.png (default docs/screenshots/reference/):

    PASS default/url-pill diff=0.03%
    FAIL default/sidebar diff=4.12% (diffs/default-sidebar-diff.png)
    NOREF default/space-bar (run with --record)

A pixel differs when its YIQ colour distance (pixelmatch's perceptual
metric) is over --delta (0.1 of the maximum) and no pixel around it in the other image
is close (1 px anti-aliasing or a sub-pixel shift is not a change). A region
fails when more than --threshold percent (default 0.5) of its pixels differ,
or when its size changed. The diff image is the reference greyed out with
the differing pixels in red. --record writes the crops as the new references
instead. Exit 1 if anything failed. Standard library only (zlib).
"""

import argparse
import json
import os
import struct
import sys
import zlib


# --- PNG (8-bit RGB / RGBA, not interlaced: what screencapture writes).

def read_png(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    pos, idat = 8, []
    width = height = channels = 0
    while pos < len(data):
        length, kind = struct.unpack('>I4s', data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', chunk)
            if depth != 8 or color not in (2, 6) or interlace:
                raise ValueError(f'{path}: only 8-bit RGB/RGBA, not interlaced')
            channels = 4 if color == 6 else 3
        elif kind == b'IDAT':
            idat.append(chunk)
        elif kind == b'IEND':
            break
        pos += 12 + length
    raw = zlib.decompress(b''.join(idat))
    stride = width * channels
    out = bytearray(stride * height)
    prev = bytearray(stride)
    i = 0
    for y in range(height):
        kind = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        if kind == 1:
            for x in range(channels, stride):
                line[x] = (line[x] + line[x - channels]) & 0xff
        elif kind == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xff
        elif kind == 3:
            for x in range(stride):
                left = line[x - channels] if x >= channels else 0
                line[x] = (line[x] + ((left + prev[x]) >> 1)) & 0xff
        elif kind == 4:
            for x in range(stride):
                a = line[x - channels] if x >= channels else 0
                b = prev[x]
                c = prev[x - channels] if x >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xff
        out[y * stride:(y + 1) * stride] = line
        prev = line
    # Always RGB from here on.
    if channels == 4:
        rgb = bytearray(width * height * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = out[0::4], out[1::4], out[2::4]
        out = rgb
    return width, height, bytes(out)


def write_png(path, width, height, rgb):
    def chunk(kind, body):
        return (struct.pack('>I', len(body)) + kind + body +
                struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff))
    stride = width * 3
    raw = b''.join(b'\x00' + rgb[y * stride:(y + 1) * stride] for y in range(height))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 6)))
        f.write(chunk(b'IEND', b''))


def crop(img, x, y, w, h):
    width, height, rgb = img
    x, y = max(0, x), max(0, y)
    w, h = max(0, min(w, width - x)), max(0, min(h, height - y))
    rows = [rgb[((y + r) * width + x) * 3:((y + r) * width + x + w) * 3] for r in range(h)]
    return w, h, b''.join(rows)


# --- The perceptual metric (pixelmatch's YIQ distance, without alpha).

MAX_DELTA = 35215.0


def yiq_delta(r1, g1, b1, r2, g2, b2):
    dy = (r1 - r2) * 0.29889531 + (g1 - g2) * 0.58662247 + (b1 - b2) * 0.11448223
    di = (r1 - r2) * 0.59597799 - (g1 - g2) * 0.27417610 - (b1 - b2) * 0.32180189
    dq = (r1 - r2) * 0.21147017 - (g1 - g2) * 0.52261711 + (b1 - b2) * 0.31114694
    return 0.5053 * dy * dy + 0.299 * di * di + 0.1957 * dq * dq


def compare(a, b, delta=0.1):
    """(changed pixel count, total, mask) for two same-size RGB images."""
    w, h, pa = a
    _, _, pb = b
    limit = delta * delta * MAX_DELTA
    mask = bytearray(w * h)
    changed = 0

    def px(buf, x, y):
        i = (y * w + x) * 3
        return buf[i], buf[i + 1], buf[i + 2]

    def close_nearby(src, x, y, other):
        r, g, bl = px(src, x, y)
        for yy in range(max(0, y - 1), min(h, y + 2)):
            for xx in range(max(0, x - 1), min(w, x + 2)):
                if yiq_delta(r, g, bl, *px(other, xx, yy)) <= limit:
                    return True
        return False

    for y in range(h):
        row = y * w * 3
        for x in range(w):
            i = row + x * 3
            if pa[i:i + 3] == pb[i:i + 3]:
                continue
            if yiq_delta(pa[i], pa[i + 1], pa[i + 2], pb[i], pb[i + 1], pb[i + 2]) <= limit:
                continue
            # Anti-aliasing or a 1 px shift: the colour is right next door.
            if close_nearby(pa, x, y, pb) and close_nearby(pb, x, y, pa):
                continue
            mask[y * w + x] = 1
            changed += 1
    return changed, w * h, mask


def diff_image(ref, mask):
    w, h, rgb = ref
    out = bytearray(w * h * 3)
    for i in range(w * h):
        if mask[i]:
            out[i * 3:i * 3 + 3] = b'\xff\x00\x00'
        else:
            r, g, b = rgb[i * 3:i * 3 + 3]
            grey = 160 + (int(0.3 * r + 0.59 * g + 0.11 * b) * 95) // 255
            out[i * 3:i * 3 + 3] = bytes((grey, grey, grey))
    return w, h, bytes(out)


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(os.path.dirname(here)))
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--shots', required=True)
    parser.add_argument('--ref', default=os.path.join(root, 'docs', 'screenshots', 'reference'))
    parser.add_argument('--out', default=None, help='where diff images go (default: <shots>/diffs)')
    parser.add_argument('--threshold', type=float, default=0.5, help='percent of pixels')
    parser.add_argument('--delta', type=float, default=0.1, help='YIQ distance, 0..1')
    parser.add_argument('--record', action='store_true')
    args = parser.parse_args(argv)
    out = args.out or os.path.join(args.shots, 'diffs')
    os.makedirs(out, exist_ok=True)
    if args.record:
        os.makedirs(args.ref, exist_ok=True)
    failed = 0
    states = sorted(f[:-5] for f in os.listdir(args.shots) if f.endswith('.json'))
    if not states:
        print(f'FAIL visual: no captures in {args.shots}')
        return 1
    for state in states:
        with open(os.path.join(args.shots, state + '.json')) as f:
            info = json.load(f)
        shot = read_png(os.path.join(args.shots, state + '.png'))
        scale = shot[0] / float(info['root'][0])
        # The root view is at the bottom of the captured frame (a title bar,
        # if any, is above it).
        top = shot[1] - round(info['root'][1] * scale)
        for name, (x, y, w, h) in sorted(info['regions'].items()):
            piece = crop(shot, round(x * scale), top + round(y * scale), round(w * scale), round(h * scale))
            label = f'{state}/{name}'
            ref_path = os.path.join(args.ref, f'{state}-{name}.png')
            if args.record:
                write_png(ref_path, *piece)
                print(f'RECORD {label} {piece[0]}x{piece[1]} -> {ref_path}')
                continue
            if not os.path.exists(ref_path):
                print(f'NOREF {label} (run with --record)')
                continue
            ref = read_png(ref_path)
            if ref[:2] != piece[:2]:
                failed += 1
                print(f'FAIL {label} size {piece[0]}x{piece[1]}, reference {ref[0]}x{ref[1]}')
                write_png(os.path.join(out, f'{state}-{name}-now.png'), *piece)
                continue
            changed, total, mask = compare(ref, piece, args.delta)
            pct = 100.0 * changed / max(1, total)
            if pct > args.threshold:
                failed += 1
                diff_path = os.path.join(out, f'{state}-{name}-diff.png')
                write_png(diff_path, *diff_image(ref, mask))
                write_png(os.path.join(out, f'{state}-{name}-now.png'), *piece)
                print(f'FAIL {label} diff={pct:.2f}% ({diff_path})')
            else:
                print(f'PASS {label} diff={pct:.2f}%')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
