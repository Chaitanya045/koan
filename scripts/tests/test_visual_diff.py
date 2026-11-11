#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/mac/visual/diff.py, the Mac suite's perceptual diff.

The captures come from the Mac (screencapture); the PNG codec, the metric
and the record / compare flow are plain Python and run in CI.
"""

import contextlib
import io
import json
import os
import pathlib
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'mac' / 'visual'))
import diff  # noqa: E402


def solid(w, h, rgb):
    return w, h, bytes(rgb) * (w * h)


def with_block(img, x0, y0, x1, y1, rgb):
    w, h, data = img
    out = bytearray(data)
    for y in range(y0, y1):
        for x in range(x0, x1):
            out[(y * w + x) * 3:(y * w + x) * 3 + 3] = bytes(rgb)
    return w, h, bytes(out)


def rgba_png(path, w, h, rgba, kind=1):
    """A PNG as screencapture writes it: RGBA, with a filtered scanline."""
    stride = w * 4
    rows = []
    for y in range(h):
        line = bytearray(rgba[y * stride:(y + 1) * stride])
        if kind == 1:
            for x in range(stride - 1, 3, -1):
                line[x] = (line[x] - line[x - 4]) & 0xff
        rows.append(bytes([kind]) + bytes(line))
    def chunk(t, body):
        return (len(body).to_bytes(4, 'big') + t + body +
                (zlib.crc32(t + body) & 0xffffffff).to_bytes(4, 'big'))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', w.to_bytes(4, 'big') + h.to_bytes(4, 'big') + bytes([8, 6, 0, 0, 0])))
        f.write(chunk(b'IDAT', zlib.compress(b''.join(rows))))
        f.write(chunk(b'IEND', b''))


class PngTest(unittest.TestCase):
    def test_round_trip(self):
        img = with_block(solid(7, 5, (10, 20, 30)), 2, 1, 4, 3, (200, 100, 50))
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'a.png')
            diff.write_png(path, *img)
            self.assertEqual(diff.read_png(path), img)

    def test_reads_filtered_rgba(self):
        rgba = bytes([1, 2, 3, 255, 50, 60, 70, 255, 9, 9, 9, 128]) * 2
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'a.png')
            rgba_png(path, 3, 2, rgba)
            w, h, rgb = diff.read_png(path)
        self.assertEqual((w, h), (3, 2))
        self.assertEqual(rgb[:9], bytes([1, 2, 3, 50, 60, 70, 9, 9, 9]))

    def test_crop_clamps_to_the_image(self):
        img = solid(4, 4, (1, 1, 1))
        self.assertEqual(diff.crop(img, 2, 2, 10, 10)[:2], (2, 2))


class CompareTest(unittest.TestCase):
    def test_same_image_is_zero(self):
        img = solid(20, 10, (240, 240, 240))
        self.assertEqual(diff.compare(img, img)[0], 0)

    def test_tiny_colour_change_is_not_a_difference(self):
        a = solid(10, 10, (240, 240, 240))
        b = solid(10, 10, (238, 241, 239))
        self.assertEqual(diff.compare(a, b)[0], 0)

    def test_one_pixel_shift_of_an_edge_is_tolerated(self):
        a = with_block(solid(20, 20, (255, 255, 255)), 5, 5, 10, 15, (0, 0, 0))
        b = with_block(solid(20, 20, (255, 255, 255)), 6, 5, 11, 15, (0, 0, 0))
        self.assertEqual(diff.compare(a, b)[0], 0)

    def test_a_new_block_is_a_difference(self):
        a = solid(20, 20, (255, 255, 255))
        b = with_block(a, 2, 2, 8, 8, (220, 30, 30))
        changed, total, _ = diff.compare(a, b)
        # All of it: a pixel is forgiven only when each image has the other's
        # colour next to it, and nothing red is near the block in `a`.
        self.assertEqual(changed, 6 * 6)
        self.assertEqual(total, 400)


class FlowTest(unittest.TestCase):
    def run_diff(self, *argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = diff.main(list(argv))
        return code, out.getvalue()

    def capture(self, shots, img):
        # A 2x capture of a 10x8 point root view, with a 2 px title bar on top.
        w, h, rgb = img
        rgba = bytearray()
        for i in range(w * h):
            rgba += rgb[i * 3:i * 3 + 3] + b'\xff'
        rgba_png(os.path.join(shots, 'default.png'), w, h, bytes(rgba))
        with open(os.path.join(shots, 'default.json'), 'w') as f:
            json.dump({'root': [10, 8], 'regions': {'box': [1, 1, 6, 6]}}, f)

    def test_record_then_pass_then_fail(self):
        base = solid(20, 18, (250, 250, 250))
        with tempfile.TemporaryDirectory() as d:
            shots, ref = os.path.join(d, 'shots'), os.path.join(d, 'ref')
            os.makedirs(shots)
            self.capture(shots, base)
            code, out = self.run_diff('--shots', shots, '--ref', ref)
            self.assertEqual(code, 0)
            self.assertIn('NOREF default/box', out)
            code, out = self.run_diff('--shots', shots, '--ref', ref, '--record')
            self.assertEqual(code, 0)
            # 6x6 points at 2x, below the 2 px title bar.
            self.assertEqual(diff.read_png(os.path.join(ref, 'default-box.png'))[:2], (12, 12))
            code, out = self.run_diff('--shots', shots, '--ref', ref)
            self.assertEqual(code, 0)
            self.assertIn('PASS default/box diff=0.00%', out)
            self.capture(shots, with_block(base, 4, 6, 12, 14, (0, 120, 255)))
            code, out = self.run_diff('--shots', shots, '--ref', ref)
            self.assertEqual(code, 1)
            self.assertRegex(out, r'FAIL default/box diff=\d+\.\d\d%')
            self.assertTrue(os.path.exists(os.path.join(shots, 'diffs', 'default-box-diff.png')))


class SameColourTest(unittest.TestCase):
    def test_band_rect_counts_from_the_far_edges(self):
        self.assertEqual(diff.band_rect([10, 20, 100, 60], [24, 2, -24, 4]), [34, 22, 52, 4])
        self.assertEqual(diff.band_rect([10, 20, 100, 60], [24, -6, -24, 4]), [34, 74, 52, 4])
        self.assertEqual(diff.band_rect([0, 0, 50, 40], [0, 0, 0, 0]), [0, 0, 50, 40])

    def test_tolerance(self):
        self.assertTrue(diff.same_colour([(24, 24, 24), (26, 23, 24)]))
        self.assertFalse(diff.same_colour([(19, 19, 19), (24, 24, 24)]))

    def capture(self, shots, img):
        # A 2x capture of a 10x8 point root view; the card is 8x6 points at 1,1.
        w, h, rgb = img
        rgba = bytearray()
        for i in range(w * h):
            rgba += rgb[i * 3:i * 3 + 3] + b'\xff'
        rgba_png(os.path.join(shots, 'card.png'), w, h, bytes(rgba))
        with open(os.path.join(shots, 'card.json'), 'w') as f:
            json.dump({'root': [10, 8], 'regions': {'card': [1, 1, 8, 6]},
                       'same': {'card': [[1, 0, -1, 1], [1, -1, -1, 1]]}}, f)

    def test_one_surface_passes_two_fail(self):
        page = solid(20, 16, (34, 36, 41))
        card = with_block(page, 2, 2, 18, 14, (24, 24, 24))
        with tempfile.TemporaryDirectory() as d:
            shots, ref = os.path.join(d, 'shots'), os.path.join(d, 'ref')
            os.makedirs(shots)
            self.capture(shots, card)
            code, out = FlowTest.run_diff(self, '--shots', shots, '--ref', ref)
            self.assertEqual(code, 0, out)
            self.assertIn('PASS card/card one surface', out)
            # The bug: a second, darker card showing above the suggestions.
            self.capture(shots, with_block(card, 2, 2, 18, 4, (19, 19, 19)))
            code, out = FlowTest.run_diff(self, '--shots', shots, '--ref', ref)
            self.assertEqual(code, 1)
            self.assertIn('FAIL card/card one surface: (19, 19, 19) vs (24, 24, 24)', out)


if __name__ == '__main__':
    unittest.main()
