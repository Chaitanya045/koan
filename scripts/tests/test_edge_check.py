#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/mac/video/edge_check.mjs, the black-edge check of the
compact_edge.mjs recordings.

A small synthetic recording (ffmpeg's lavfi) stands in for record.sh's: a
white page with a dark sidebar, and, in one variant, a dark notch flashing
at the top edge for a few frames, the glitch the check exists for. Skipped
where node or ffmpeg is missing (the Mac bench has both).
"""

import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

CHECK = (pathlib.Path(__file__).resolve().parent.parent / 'mac' / 'video' /
         'edge_check.mjs')
NODE = shutil.which('node')
FFMPEG = shutil.which('ffmpeg')

# The window, in points; the recording is at 2 device pixels per point.
W, H, SCALE = 200, 120, 2
SIDEBAR_W = 40


@unittest.skipUnless(NODE and FFMPEG, 'needs node and ffmpeg')
class EdgeCheckTest(unittest.TestCase):

    def record(self, tmp, boxes):
        """Writes a 3 s video, its .rec.json and the marks of one peek-in at
        1 s, and returns (video, marks). `boxes` are drawbox filters (device
        pixels) on top of the white page."""
        video = os.path.join(tmp, 'edge.mp4')
        # The sidebar is out once the peek has ended (from 1.3 s), dark.
        filters = [f'drawbox=x=0:y=0:w={SIDEBAR_W * SCALE}:h=ih:color=0x141414:t=fill:'
                   "enable='gte(t,1.3)'"] + boxes
        subprocess.run([
            FFMPEG, '-v', 'error', '-y', '-f', 'lavfi', '-i',
            f'color=c=white:s={W * SCALE}x{H * SCALE}:r=30:d=3',
            '-vf', ','.join(filters), '-pix_fmt', 'yuv420p', video
        ], check=True)
        with open(video + '.rec.json', 'w') as f:
            json.dump({'rect': {'x': 0, 'y': 0, 'w': W, 'h': H},
                       'scale': SCALE, 'fps': 30, 't0': 0}, f)
        marks = os.path.join(tmp, 'edge.marks.json')
        with open(marks, 'w') as f:
            json.dump({'marks': [{
                't': 1000, 'round': 1, 'label': 'peek-in', 'action': True,
                'rect': {'x': 0, 'y': 0, 'w': W, 'h': H},
                'sidebarW': SIDEBAR_W, 'pointer': [[140, 60], [1, 60]],
            }]}, f)
        return video, marks

    def check(self, boxes):
        with tempfile.TemporaryDirectory() as tmp:
            video, marks = self.record(tmp, boxes)
            return subprocess.run(
                [NODE, str(CHECK), video, marks],
                env={**os.environ, 'FFMPEG': FFMPEG},
                capture_output=True, text=True)

    def test_passes_without_a_glitch(self):
        # The sidebar slides in over its own columns: not a glitch.
        r = self.check([])
        self.assertEqual(0, r.returncode, r.stdout + r.stderr)
        self.assertIn('PASS', r.stdout)

    def test_fails_on_a_dark_notch_at_the_top(self):
        # Upstream's floating corner: 8 points square, beside where the
        # sidebar will be, for a tenth of a second.
        notch = (f'drawbox=x={SIDEBAR_W * SCALE}:y=0:w=16:h=16:color=black:'
                 "t=fill:enable='between(t,1.1,1.2)'")
        r = self.check([notch])
        self.assertEqual(1, r.returncode, r.stdout + r.stderr)
        self.assertIn('BLACK', r.stdout)
        self.assertIn('"top"', r.stdout)

    def test_fails_on_a_black_left_edge_that_stays(self):
        # A column at the window's left edge left black after the slide (in
        # the sidebar's columns, checked once the slide is over), here with
        # the sidebar hidden at the end: a peek-out.
        with tempfile.TemporaryDirectory() as tmp:
            video = os.path.join(tmp, 'edge.mp4')
            subprocess.run([
                FFMPEG, '-v', 'error', '-y', '-f', 'lavfi', '-i',
                f'color=c=white:s={W * SCALE}x{H * SCALE}:r=30:d=3', '-vf',
                f'drawbox=x=0:y=0:w={SIDEBAR_W * SCALE}:h=ih:color=0x141414:'
                "t=fill:enable='lt(t,1.2)',"
                "drawbox=x=0:y=0:w=4:h=ih:color=black:t=fill:"
                "enable='between(t,1.2,2.2)'",
                '-pix_fmt', 'yuv420p', video
            ], check=True)
            with open(video + '.rec.json', 'w') as f:
                json.dump({'rect': {'x': 0, 'y': 0, 'w': W, 'h': H},
                           'scale': SCALE, 'fps': 30, 't0': 0}, f)
            marks = os.path.join(tmp, 'edge.marks.json')
            with open(marks, 'w') as f:
                json.dump({'marks': [{
                    't': 1000, 'round': 1, 'label': 'peek-out', 'action': True,
                    'rect': {'x': 0, 'y': 0, 'w': W, 'h': H},
                    'sidebarW': SIDEBAR_W, 'pointer': [[1, 60], [140, 60]],
                }]}, f)
            r = subprocess.run([NODE, str(CHECK), video, marks],
                               env={**os.environ, 'FFMPEG': FFMPEG},
                               capture_output=True, text=True)
        self.assertEqual(1, r.returncode, r.stdout + r.stderr)
        self.assertIn('"left"', r.stdout)


if __name__ == '__main__':
    unittest.main()
