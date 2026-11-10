#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Checks Koan's vector icons (src/**/*.icon) against how Chromium paints them.

ui/gfx/paint_vector_icon.cc applies an icon's CLIP to the whole canvas once,
after every path is built, not to the paths that follow it. In an icon with
more than one path, a CLIP meant for one of them clips them all (#92: the
Library icon lost its box). So CLIP is only allowed in single-path icons.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent


def strip_comments(text):
    return re.sub(r'//[^\n]*', '', text)


def clip_problems(text):
    """Returns why the icon's CLIP is a problem, per size it defines."""
    problems = []
    # An .icon may define several sizes, each starting at CANVAS_DIMENSIONS.
    for rep in re.split(r'\bCANVAS_DIMENSIONS\b', strip_comments(text)):
        if re.search(r'\bCLIP\b', rep) and re.search(r'\bNEW_PATH\b', rep):
            problems.append('CLIP in an icon with more than one path')
    return problems


class ClipRuleTest(unittest.TestCase):

    def test_rule(self):
        self.assertEqual(clip_problems('CANVAS_DIMENSIONS, 24,\nCLIP, 0, 0, '
                                       '24, 8,\nCIRCLE, 4, 4, 2'), [])
        self.assertEqual(
            len(clip_problems('CANVAS_DIMENSIONS, 24,\nCIRCLE, 4, 4, 2,\n'
                              'NEW_PATH,\nCLIP, 0, 0, 24, 8,\n'
                              'CIRCLE, 8, 8, 2')), 1)
        # A comment that names CLIP is not one.
        self.assertEqual(clip_problems('// no CLIP here\nCIRCLE, 1, 1, 1,\n'
                                       'NEW_PATH,\nCIRCLE, 2, 2, 1'), [])

    def test_koan_icons(self):
        icons = sorted((ROOT / 'src').rglob('*.icon'))
        self.assertTrue(icons)
        for icon in icons:
            with self.subTest(icon=str(icon.relative_to(ROOT))):
                self.assertEqual(clip_problems(icon.read_text()), [])


if __name__ == '__main__':
    unittest.main()
