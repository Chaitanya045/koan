#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for the user flows' recording checks and index
(scripts/mac/video/uf_post.mjs, uf_index.mjs; run by run_flows.sh).

The still check must pass on a page that doesn't change, fail on a one-frame
flicker inside the rect (what a capture that resized or re-laid out the
person's tab would show), and its control must see a change made on
purpose. A synthetic recording (ffmpeg's lavfi) stands in for record.sh's.
The index must list every flow and step with its result, the first failing
check and its links. Skipped where node (or, for the recordings, ffmpeg) is
missing; the Mac bench has both.
"""

import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

VIDEO = pathlib.Path(__file__).resolve().parent.parent / 'mac' / 'video'
NODE = shutil.which('node')
FFMPEG = shutil.which('ffmpeg')
T0 = 1_700_000_000_000  # the video's time 0 on the wall clock (ms)
W, H, SCALE = 200, 120, 2


def node(*args):
  return subprocess.run([NODE, *map(str, args)], capture_output=True, text=True, timeout=300)


def step(id, start, ms):
  return {'id': id, 'title': id, 'ok': True, 'checks': [{'name': 'x', 'ok': True}], 'shots': [], 'startedAt': start,
          'ms': ms, 'agent': {'calls': 0, 'ms': 0}, 'needs': [], 'skipped': None}


@unittest.skipUnless(NODE and FFMPEG, 'needs node and ffmpeg')
class StillCheckTest(unittest.TestCase):

  def setUp(self):
    self.tmp = tempfile.mkdtemp()
    self.addCleanup(shutil.rmtree, self.tmp)

  def flow(self, boxes):
    """A 3.5 s recording of a white page with `boxes` (drawbox filters, device
    pixels), its rec.json, the marks of a still range (1.0-2.0 s) and a
    control range (2.1-2.9 s, where the page changes colour on purpose), and
    a summary with the two steps they belong to."""
    video = os.path.join(self.tmp, 'video.mp4')
    control = ('drawbox=x=0:y=0:w=400:h=240:color=0xfde2e2:t=fill:enable=\'between(t,2.3,2.5)\','
               'drawbox=x=0:y=0:w=400:h=240:color=0xe2f0fd:t=fill:enable=\'between(t,2.6,2.8)\'')
    vf = ','.join([*boxes, control])
    subprocess.run([FFMPEG, '-v', 'error', '-y', '-f', 'lavfi', '-i', f'color=white:s={W * SCALE}x{H * SCALE}:r=30',
                    '-t', '3.5', '-vf', vf, '-pix_fmt', 'yuv420p', '-c:v', 'libx264', '-crf', '18', video],
                   check=True, timeout=120)
    pathlib.Path(video + '.rec.json').write_text(json.dumps(
        {'file': video, 'rect': {'x': 0, 'y': 0, 'w': W, 'h': H}, 'scale': SCALE, 'fps': 30, 't0': T0}))
    rect = {'x': 10, 'y': 10, 'w': 150, 'h': 90}
    marks = {'marks': [
        {'t': T0 + 1000, 'label': 'capture-user-tab', 'action': True, 'stillRect': rect},
        {'t': T0 + 2000, 'label': 'capture-user-tab-end', 'stillRect': rect, 'still': {'from': T0 + 1000, 'to': T0 + 2000}},
        {'t': T0 + 2100, 'label': 'still-control', 'action': True, 'stillRect': rect, 'control': True},
        {'t': T0 + 2900, 'label': 'still-control-end', 'stillRect': rect, 'control': True,
         'still': {'from': T0 + 2100, 'to': T0 + 2900}},
    ]}
    pathlib.Path(self.tmp, 'marks.json').write_text(json.dumps(marks))
    summary = {'flow': 'uf_shots', 'status': 'pass', 'steps': [step('S-4', T0 + 900, 1200), step('S-5', T0 + 2050, 1000)],
               'agent': {'calls': 0, 'ms': 0}, 'info': []}
    pathlib.Path(self.tmp, 'summary.json').write_text(json.dumps(summary))
    r = node(VIDEO / 'uf_post.mjs', 'still', self.tmp, video)
    self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
    s = json.loads(pathlib.Path(self.tmp, 'summary.json').read_text())
    return {x['id']: x for x in s['steps']}, s, r.stdout

  def test_a_still_page_passes_and_the_control_sees_its_change(self):
    steps, s, out = self.flow([])
    self.assertTrue(steps['S-4.still']['ok'], out)
    self.assertTrue(steps['S-5.still-control']['ok'], out)
    self.assertEqual(s['status'], 'pass')

  def test_a_one_frame_flicker_fails(self):
    # Two frames (1.40-1.47 s) of a grey band across the rect: a re-layout.
    steps, s, out = self.flow(["drawbox=x=40:y=60:w=200:h=40:color=0x808080:t=fill:enable='between(t,1.40,1.47)'"])
    self.assertFalse(steps['S-4.still']['ok'], out)
    self.assertIn('most changed in one frame', steps['S-4.still']['checks'][0]['detail'])
    self.assertEqual(s['status'], 'fail')

  def test_a_flicker_outside_the_rect_is_left_out(self):
    # The pill or the sidebar at the window's edge, outside the page's rect.
    steps, _, out = self.flow(["drawbox=x=360:y=0:w=40:h=240:color=0x000000:t=fill:enable='between(t,1.40,1.47)'"])
    self.assertTrue(steps['S-4.still']['ok'], out)


@unittest.skipUnless(NODE, 'needs node')
class SummaryAndIndexTest(unittest.TestCase):

  def setUp(self):
    self.tmp = tempfile.mkdtemp()
    self.addCleanup(shutil.rmtree, self.tmp)

  def test_missing_summary_is_a_failure(self):
    d = os.path.join(self.tmp, 'uf_x')
    os.makedirs(d)
    r = node(VIDEO / 'uf_post.mjs', 'missing', d, 'uf_x', 1)
    self.assertEqual(r.returncode, 0, r.stderr)
    s = json.loads(pathlib.Path(d, 'summary.json').read_text())
    self.assertEqual(s['status'], 'fail')
    self.assertIn('exited 1', s['stopped'])

  def test_index(self):
    good = {'flow': 'uf_a', 'title': 'A flow', 'feature': '#1', 'status': 'pass', 'wallMs': 61000,
            'agent': {'calls': 3, 'ms': 2500}, 'info': ['3-page signup: 2 koan calls'],
            'steps': [dict(step('UF-A-1', 0, 1500), shots=[{'path': 'shots/01-UF-A-1-x.png', 'label': 'x'}])]}
    bad_step = step('UF-B-2', 0, 900)
    bad_step.update(ok=False, checks=[{'name': 'the pill shows', 'ok': False, 'detail': 'no pill'}])
    skip_step = dict(step('UF-B-3', 0, 10), ok=None, skipped='needs real input')
    bad = {'flow': 'uf_b', 'title': 'B flow', 'feature': '#2', 'status': 'fail', 'wallMs': 5000,
           'agent': {'calls': 1, 'ms': 100}, 'info': [], 'steps': [step('UF-B-1', 0, 1), bad_step, skip_step]}
    for s in (good, bad):
      d = pathlib.Path(self.tmp, s['flow'])
      (d / 'sheets').mkdir(parents=True)
      (d / 'summary.json').write_text(json.dumps(s))
    pathlib.Path(self.tmp, 'uf_a', 'sheets', 'sheets.tsv').write_text(
        'sheet\tfrom\tto\tframes\tmax_changed_px\tchanged_frames\tmarks\n'
        f'{self.tmp}/uf_a/sheets/0001.000-r01-step_UF-A-1/sheet_000.png\t1.0\t2.6\t16\t5000\t3\tstep:UF-A-1,calibrate\n')
    r = node(VIDEO / 'uf_index.mjs', self.tmp, '--app', '/x/Koan.app')
    self.assertEqual(r.returncode, 0, r.stderr)
    md = r.stdout
    self.assertIn('**2 flows: 1 passed, 1 failed, 0 skipped, 0 stopped.**', md)
    self.assertIn('Steps: 2 passed, 1 failed, 1 skipped.', md)
    self.assertIn('| UF-B-2 | UF-B-2 | **FAIL** | the pill shows -- no pill |', md)
    self.assertIn('| UF-B-3 | UF-B-3 | SKIP: needs real input |', md)
    self.assertIn('[x](uf_a/shots/01-UF-A-1-x.png)', md)
    self.assertIn('(uf_a/sheets/0001.000-r01-step_UF-A-1/sheet_000.png)', md)
    self.assertIn('- 3-page signup: 2 koan calls', md)
    self.assertIn('- UF-B-2: the pill shows -- `no pill`', md)


if __name__ == '__main__':
  unittest.main()
