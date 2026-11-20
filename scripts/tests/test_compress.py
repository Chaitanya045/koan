#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/mac/video/compress.sh, the review tool that makes the
user flows' screenshots, recordings and contact sheets smaller.

The size math and the argument handling run through --plan (no encoder), so
they need only bash and awk. The real encodes (a synthetic still and video
from ffmpeg's lavfi, checked with --check) are skipped where ffmpeg is
missing; the Mac bench has it.
"""

import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

TOOL = (pathlib.Path(__file__).resolve().parent.parent / 'mac' / 'video' /
        'compress.sh')
BASH = shutil.which('bash')
FFMPEG = shutil.which('ffmpeg')
FFPROBE = shutil.which('ffprobe')


def run(*args, env=None, cwd=None):
  full = dict(os.environ)
  full.update(env or {})
  return subprocess.run([BASH, str(TOOL), *args], capture_output=True,
                        text=True, env=full, cwd=cwd, timeout=120)


@unittest.skipUnless(BASH, 'needs bash')
class PlanTest(unittest.TestCase):
  """--plan --in-size: what compress.sh would make, without an encoder."""

  def setUp(self):
    self.tmp = tempfile.mkdtemp()
    self.addCleanup(shutil.rmtree, self.tmp)

  def file(self, name):
    path = os.path.join(self.tmp, name)
    pathlib.Path(path).write_bytes(b'')
    return path

  def plan(self, name, size, *opts):
    r = run('--plan', '--in-size', size, *opts, self.file(name))
    self.assertEqual(r.returncode, 0, r.stderr)
    fields = r.stdout.strip().split('\t')
    self.assertEqual(fields[0], 'plan', r.stdout)
    return {'kind': fields[2], 'in': fields[3], 'out': fields[5],
            'codec': fields[6], 'path': fields[7]}

  def test_still_fits_1080_by_default(self):
    p = self.plan('w.png', '2560x1640')
    self.assertEqual((p['kind'], p['out'], p['codec']), ('still', '1686x1080', 'png'))
    self.assertTrue(p['path'].endswith('/w.1080.png'), p['path'])

  def test_still_720_for_overviews(self):
    self.assertEqual(self.plan('w.png', '2560x1640', '--720')['out'], '1124x720')

  def test_still_never_enlarged(self):
    p = self.plan('small.png', '800x600')
    self.assertEqual(p['out'], '800x600')
    self.assertEqual(self.plan('small.png', '800x600', '--720')['out'], '800x600')

  def test_still_wide_limited_by_its_width(self):
    # A full Retina screen (1470x956 points at 2x).
    self.assertEqual(self.plan('s.png', '2940x1912')['out'], '1661x1080')
    self.assertEqual(self.plan('wide.png', '4000x1000')['out'], '1920x480')

  def test_portrait_turns_the_box(self):
    self.assertEqual(self.plan('tall.png', '1640x2560')['out'], '1080x1686')
    self.assertEqual(self.plan('tall.png', '1640x2560', '--720')['out'], '720x1124')

  def test_jpeg(self):
    p = self.plan('w.png', '2560x1640', '--jpeg')
    self.assertEqual(p['codec'], 'jpeg')
    self.assertTrue(p['path'].endswith('/w.1080.jpg'), p['path'])
    self.assertEqual(self.plan('w.jpg', '1920x1080')['out'], '1920x1080')

  def test_video_720_by_default_even(self):
    p = self.plan('rec.mp4', '2560x1640')
    self.assertEqual((p['kind'], p['out']), ('video', '1124x720'))
    self.assertTrue(p['path'].endswith('/rec.720.mp4'), p['path'])
    self.assertEqual(self.plan('rec.mp4', '2560x1640', '--1080')['out'], '1686x1080')
    # Odd sizes come out even (H.264 4:2:0), never larger.
    self.assertEqual(self.plan('rec.mp4', '1281x721')['out'], '1278x720')
    self.assertEqual(self.plan('rec.mov', '641x481')['out'], '640x480')

  def test_video_codec(self):
    self.assertEqual(self.plan('rec.mp4', '2560x1640', '--encoder', 'libx264')['codec'], 'libx264')
    self.assertEqual(self.plan('rec.mp4', '2560x1640', '--encoder', 'videotoolbox')['codec'],
                     'h264_videotoolbox')

  def test_sheet_fits_1280_wide(self):
    self.assertEqual(self.plan('sheet_000.png', '1500x1500', '--sheet')['out'], '1280x1280')
    self.assertEqual(self.plan('sheet_000.png', '1000x800', '--sheet')['out'], '1000x800')
    p = self.plan('sheet_000.png', '1500x900', '--sheet')
    self.assertEqual((p['kind'], p['out']), ('sheet', '1280x768'))
    self.assertTrue(p['path'].endswith('/sheet_000.sheet.png'), p['path'])

  def test_out_and_out_dir(self):
    r = run('--plan', '--in-size', '2560x1640', '-o', os.path.join(self.tmp, 'x.png'), self.file('a.png'))
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertTrue(r.stdout.strip().endswith('/x.png'), r.stdout)
    d = os.path.join(self.tmp, 'out')
    r = run('--plan', '--in-size', '100x100', '--out-dir', d, self.file('a.png'), self.file('b.png'))
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertEqual([l.split('\t')[7] for l in r.stdout.strip().split('\n')],
                     [f'{d}/a.1080.png', f'{d}/b.1080.png'])

  def test_reads_the_size_with_ffprobe(self):
    fake = os.path.join(self.tmp, 'ffprobe')
    pathlib.Path(fake).write_text('#!/bin/sh\necho 2940x1912\n')
    os.chmod(fake, 0o755)
    r = run('--plan', self.file('s.png'), env={'FFPROBE': fake})
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertIn('\t2940x1912\t->\t1661x1080\t', r.stdout)

  def test_bad_ffprobe_answer_fails(self):
    fake = os.path.join(self.tmp, 'ffprobe')
    pathlib.Path(fake).write_text('#!/bin/sh\necho nonsense\n')
    os.chmod(fake, 0o755)
    r = run('--plan', self.file('s.png'), env={'FFPROBE': fake})
    self.assertEqual(r.returncode, 5, r.stdout + r.stderr)
    self.assertIn('could not read the size', r.stderr)


@unittest.skipUnless(BASH, 'needs bash')
class ArgumentTest(unittest.TestCase):
  """Bad arguments: exit 2 with a message, nothing written."""

  def setUp(self):
    self.tmp = tempfile.mkdtemp()
    self.addCleanup(shutil.rmtree, self.tmp)
    self.png = os.path.join(self.tmp, 'a.png')
    self.mp4 = os.path.join(self.tmp, 'a.mp4')
    for f in (self.png, self.mp4):
      pathlib.Path(f).write_bytes(b'')

  def refused(self, *args, says):
    r = run(*args)
    self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
    self.assertIn(says, r.stderr)
    self.assertEqual(sorted(os.listdir(self.tmp)), ['a.mp4', 'a.png'])

  def test_refusals(self):
    self.refused(says='no input files')
    self.refused('--bogus', self.png, says='unknown option --bogus')
    self.refused('--720', '--1080', self.png, says='not both')
    self.refused('-o', 'x.png', self.png, self.mp4, says='one output')
    self.refused('--in-size', '10x10', self.png, says='--plan only')
    self.refused('--plan', '--in-size', 'big', self.png, says='--in-size wants WxH')
    self.refused('--strict', self.png, says='--strict needs --check')
    self.refused('--plan', '--in-size', '10x10', os.path.join(self.tmp, 'none.png'), says='no such file')
    self.refused('--plan', '--in-size', '10x10', '--jpeg', self.mp4, says='--jpeg is for stills')
    self.refused('--plan', '--in-size', '10x10', '--sheet', self.mp4, says='--sheet takes .png sheets')
    self.refused('--plan', '--in-size', '10x10', '--sheet', '--720', self.png, says='its own size')
    self.refused('--plan', '--encoder', 'qsv', self.png, says='unknown encoder')
    self.refused('-o', self.png, '--plan', '--in-size', '10x10', self.png, says='overwrite the input')
    self.refused('--replace', '--jpeg', '--plan', '--in-size', '10x10', self.png, says='keeps the format')
    self.refused('--replace', '--plan', '--in-size', '10x10', self.mp4, says='--replace is for stills')
    txt = os.path.join(self.tmp, 'a.txt')
    pathlib.Path(txt).write_bytes(b'')
    r = run('--plan', '--in-size', '10x10', txt)
    os.remove(txt)
    self.assertEqual(r.returncode, 2)
    self.assertIn("don't know what", r.stderr)

  def test_no_ffmpeg(self):
    r = run(self.png, env={'FFMPEG': '/nonexistent/ffmpeg', 'FFPROBE': '/nonexistent/ffprobe'})
    self.assertEqual(r.returncode, 3, r.stderr)
    self.assertIn('no ffprobe', r.stderr)


@unittest.skipUnless(BASH and FFMPEG and FFPROBE, 'needs ffmpeg and ffprobe')
class EncodeTest(unittest.TestCase):
  """Real encodes of synthetic media, checked with --check."""

  def setUp(self):
    self.tmp = tempfile.mkdtemp()
    self.addCleanup(shutil.rmtree, self.tmp)

  def lavfi(self, name, *args):
    path = os.path.join(self.tmp, name)
    subprocess.run([FFMPEG, '-v', 'error', '-y', '-f', 'lavfi', *args, path], check=True, timeout=120)
    return path

  def probe(self, path):
    out = subprocess.run([FFPROBE, '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                          'stream=width,height,avg_frame_rate', '-of', 'csv=p=0', path],
                         capture_output=True, text=True, check=True).stdout.strip()
    return out

  def check_line(self, stdout):
    line = [l for l in stdout.split('\n') if l.startswith('check\t')]
    self.assertEqual(len(line), 1, stdout)
    return line[0].split('\t')

  def test_still(self):
    src = self.lavfi('w.png', '-i', 'testsrc2=size=2560x1640', '-frames:v', '1')
    r = run('--check', '--strict', src)
    self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
    f = self.check_line(r.stdout)
    self.assertEqual((f[4], f[5], f[8]), ('2560x1640', '1686x1080', 'ok'), f)
    self.assertGreaterEqual(float(f[6].split('=')[1]), 0.97, f)
    self.assertLess(int(f[3]), int(f[2]), 'the output is smaller')
    self.assertEqual(self.probe(os.path.join(self.tmp, 'w.1080.png')).split(',')[:2], ['1686', '1080'])

  def test_still_small_is_copied(self):
    src = self.lavfi('s.png', '-i', 'testsrc2=size=640x400', '-frames:v', '1')
    r = run('-o', os.path.join(self.tmp, 'o.png'), src)
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertEqual(pathlib.Path(src).read_bytes(), pathlib.Path(self.tmp, 'o.png').read_bytes())

  def test_overview_warns_strict_fails(self):
    src = self.lavfi('w.png', '-i', 'testsrc2=size=2940x1912', '-frames:v', '1')
    r = run('--720', '--check', '--strict', src)
    self.assertEqual(r.returncode, 4, r.stdout + r.stderr)
    self.assertIn('more than 2x smaller', r.stdout)

  def test_video_keeps_the_frame_rate(self):
    src = self.lavfi('rec.mp4', '-i', 'testsrc2=size=2560x1640:rate=30', '-t', '2', '-pix_fmt', 'yuv420p')
    pathlib.Path(src + '.rec.json').write_text(json.dumps({'file': src, 'scale': 2, 't0': 5}))
    r = run('--check', '--strict', '--encoder', 'libx264', src)
    self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
    out = os.path.join(self.tmp, 'rec.720.mp4')
    self.assertEqual(self.probe(out), '1124,720,30/1')
    f = self.check_line(r.stdout)
    self.assertGreaterEqual(float(f[6].split('=')[1]), 0.95, f)
    rec = json.loads(pathlib.Path(out + '.rec.json').read_text())
    self.assertAlmostEqual(rec['scale'], 2 * 1124 / 2560)
    self.assertEqual((rec['t0'], rec['file']), (5, out))


if __name__ == '__main__':
  unittest.main()
