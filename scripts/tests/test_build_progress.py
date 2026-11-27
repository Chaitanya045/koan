#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/ci/build_progress.py, with the numbers of the chained
macOS run 45816128161 (issue #534)."""

import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts' / 'ci' / 'build_progress.py'
sys.path.insert(0, str(SCRIPT.parent))
import build_progress  # noqa: E402

# The end of a job's build.log that stopped at the deadline.
END = b"""[27999/80255] 4h45m18.70s F CXX obj/a.o
[28000/80255] 4h45m18.71s - CXX obj/third_party/jsoncpp/jsoncpp/json_writer.o
deadline reached: stopping the build
noexec:128 local:27872 remote:0 cache:0 skip:17442
4h45m20.68s Build Failure: 28000 done, 0 failed, 52255 remaining - 1.64/s
 interrupt by signal
"""


def start(done, total):
  """The first minutes of the next job's build.log."""
  return (b'Done. Made 37386 targets from 5091 files in 20585ms\n'
          b'[0/90434] 24.34s S ACTION //a:b\n'
          b'[%d/%d] 5m0.00s F CXX obj/b.o\n' % (done, total))


class ParseTest(unittest.TestCase):

  def test_remaining_from_the_summary(self):
    self.assertEqual(build_progress.remaining(END), 52255)

  def test_remaining_from_the_last_progress_line(self):
    # A job killed before siso printed its summary.
    log = b'[10/100] 1.0s S CXX a.o\n[12/90] 2.0s F CXX a.o\n'
    self.assertEqual(build_progress.remaining(log), 78)

  def test_no_progress(self):
    self.assertIsNone(build_progress.remaining(b'gn gen failed\n'))
    self.assertIsNone(build_progress.last_progress(b''))

  def test_run_45816128161(self):
    # build-02 after build-01: healthy. build-03 and build-04: rebuilding.
    self.assertFalse(build_progress.too_many(49796, 52255))
    self.assertTrue(build_progress.too_many(68900, 36348))
    self.assertTrue(build_progress.too_many(66634, 39437))

  def test_small_remainders_have_slack(self):
    self.assertFalse(build_progress.too_many(1500, 600))
    self.assertTrue(build_progress.too_many(3000, 600))


class CommandTest(unittest.TestCase):

  def setUp(self):
    self.dir = pathlib.Path(tempfile.mkdtemp())
    self.addCleanup(lambda: subprocess.run(['rm', '-rf', str(self.dir)]))
    self.state = self.dir / 'out' / 'koan-ci-resume'
    self.state.parent.mkdir()

  def run_script(self, command, log, run='111'):
    path = self.dir / 'build.log'
    path.write_bytes(log)
    env = dict(os.environ, GITHUB_RUN_ID=run)
    return subprocess.run(
        [sys.executable, '-I', str(SCRIPT), command, str(path),
         str(self.state)], env=env, capture_output=True, text=True)

  def test_record_then_a_healthy_resume(self):
    self.assertEqual(self.run_script('record', END).returncode, 0)
    self.assertEqual(self.state.read_text(), 'run=111\nremaining=52255\n')
    r = self.run_script('check', start(754, 49796))
    self.assertEqual(r.returncode, 0, r.stdout)
    self.assertIn('ok', r.stdout)

  def test_a_resume_that_rebuilds_fails_loudly(self):
    self.state.write_text('run=111\nremaining=36348\n')
    r = self.run_script('check', start(1209, 68900))
    self.assertEqual(r.returncode, 1)
    self.assertIn('::error::', r.stdout)
    self.assertIn('36348', r.stdout)

  def test_no_progress_yet(self):
    self.state.write_text('run=111\nremaining=36348\n')
    self.assertEqual(self.run_script('check', b'loading...\n').returncode, 2)

  def test_nothing_to_compare(self):
    # Slice 1 of a full build: no state.
    self.assertEqual(self.run_script('check', start(1, 90000)).returncode, 0)
    # Slice 1 of an incremental build: the base run's out directory.
    self.state.write_text('run=99\nremaining=100\n')
    r = self.run_script('check', start(1, 90000))
    self.assertEqual(r.returncode, 0)
    self.assertIn('not this one', r.stdout)

  def test_record_without_progress_writes_nothing(self):
    self.assertEqual(self.run_script('record', b'nothing\n').returncode, 0)
    self.assertFalse(self.state.exists())


if __name__ == '__main__':
  unittest.main()
