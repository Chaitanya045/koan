#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for the official Mac build: scripts/mac/build_official.sh's
judgements, job choices, load watchdog and --dry-run plan, the lock helper
scripts/mac/tools/flock_run.py, and build/koan-mac-official.gn.

Runs on Linux: the Mac probes are faked with KOAN_FAKE_*.
"""

import fcntl
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SCRIPT = ROOT / 'scripts' / 'mac' / 'build_official.sh'
FLOCK_RUN = ROOT / 'scripts' / 'mac' / 'tools' / 'flock_run.py'
ARGS = ROOT / 'build' / 'koan-mac-official.gn'

GOOD_MAC = {
    'KOAN_FAKE_OS': 'mac',
    'KOAN_FAKE_ARCH': 'arm64',
    'KOAN_FAKE_MACOS': '27.0',
    'KOAN_FAKE_XCODE': 'Xcode 27.1',
    'KOAN_FAKE_XCODE_LICENSE': '0',
    'KOAN_FAKE_METAL': '/fake/metal',
    'KOAN_FAKE_DISK_GB': '167',
    'KOAN_FAKE_RAM_GB': '16',
    'KOAN_FAKE_SDK': '27.0',
    'KOAN_FAKE_CPUS': '10',
    'KOAN_FAKE_AVAIL_GB': '7',
}


def call(func, *args):
  """Runs a function of build_official.sh: (exit code, output)."""
  r = subprocess.run(
      ['bash', '-c', f'source "{SCRIPT}"; {func} "$@"', 'x', *args],
      capture_output=True, text=True, check=False)
  return r.returncode, r.stdout.strip()


def out(func, *args):
  rc, text = call(func, *args)
  assert rc == 0, (func, args, text)
  return text


class JudgeTest(unittest.TestCase):

  def assertVerdict(self, verdict, check, *args):
    rc, text = call(f'koan_judge_{check}', *args)
    self.assertTrue(text.startswith(verdict), text)
    self.assertEqual(rc, 1 if verdict == 'FAIL' else 0, text)

  def test_disk_sized_for_a_256_gb_air(self):
    # About 167 GB free after a cleanup on the maintainer's Air.
    self.assertVerdict('ok', 'official_disk', '167', '0')
    self.assertVerdict('warn', 'official_disk', '80', '0')
    self.assertVerdict('FAIL', 'official_disk', '60', '0')
    # What the out dir already holds counts, down to a relink's minimum.
    self.assertVerdict('ok', 'official_disk', '90', '30')
    self.assertVerdict('FAIL', 'official_disk', '30', '30')
    rc, text = call('koan_judge_official_disk', '24', '60')
    self.assertEqual(rc, 1)
    self.assertIn('needs about 25 GB', text)

  def test_ram(self):
    self.assertVerdict('FAIL', 'official_ram', '8')
    self.assertVerdict('warn', 'official_ram', '16')
    self.assertVerdict('ok', 'official_ram', '32')

  def test_sdk(self):
    self.assertVerdict('ok', 'sdk', '27.0', '27.0')
    self.assertVerdict('ok', 'sdk', '27.0.1', '27.0')
    self.assertVerdict('warn', 'sdk', '26.2', '27.0')
    self.assertVerdict('FAIL', 'sdk', '', '27.0')


class HelperTest(unittest.TestCase):

  def test_thin_lto_jobs_follow_the_ram(self):
    self.assertEqual(out('koan_thin_lto_jobs_for_ram', '16'), '4')
    self.assertEqual(out('koan_thin_lto_jobs_for_ram', '24'), '6')
    self.assertEqual(out('koan_thin_lto_jobs_for_ram', '32'), '8')
    self.assertEqual(out('koan_thin_lto_jobs_for_ram', '64'), '0')

  def test_jobs_respect_the_house_rules(self):
    jobs = lambda *a: out('koan_official_jobs', *a)
    # At most 4 while the user may be at the Mac, fewer when memory is short.
    self.assertEqual(jobs('0', '10', '12'), '4')
    self.assertEqual(jobs('0', '10', '4'), '3')
    self.assertEqual(jobs('0', '10', '2'), '2')
    self.assertEqual(jobs('0', '10', '12', '8'), '4')
    # 8 only when away.
    self.assertEqual(jobs('1', '10', '12'), '8')
    self.assertEqual(jobs('1', '10', '7'), '6')
    self.assertEqual(jobs('1', '10', '12', '12'), '8')
    self.assertEqual(jobs('1', '4', '12'), '4')

  def test_watchdog_pauses_only_when_the_load_stays_high(self):
    state = ['0', '0', '0']
    actions = []
    for load in ['9.1', '9.5', '3.0', '9.0', '10', '12', '11']:
      p, h, o, a = out('koan_pause_step', state[0], load, '8', '4.8',
                       state[1], state[2]).split()
      state = [p, h, o]
      actions.append(a)
    # A dip resets the count; four high samples in a row stop the build.
    self.assertEqual(actions, ['-', '-', '-', '-', '-', '-', 'stop'])
    self.assertEqual(state[0], '1')
    for load, want in [('6', '-'), ('4', '-'), ('7', '-'), ('4', '-'),
                       ('3', 'cont')]:
      p, h, o, a = out('koan_pause_step', state[0], load, '8', '4.8',
                       state[1], state[2]).split()
      state = [p, h, o]
      self.assertEqual(a, want, load)
    self.assertEqual(state[0], '0')

  def test_last_thinlto_jobs_wins(self):
    line = ('ldflags = -flto=thin -Wl,--thinlto-jobs=all '
            '-Wl,-mllvm,-import-instr-limit=30 -Wl,--thinlto-jobs=4')
    self.assertEqual(out('koan_last_thinlto_jobs', line), '4')
    self.assertEqual(out('koan_last_thinlto_jobs', 'ldflags = -Wl,--thinlto-jobs=all'),
                     'all')


class DryRunTest(unittest.TestCase):

  def plan(self, *args, **env):
    full = {k: v for k, v in os.environ.items()
            if not k.startswith('KOAN_') and k not in ('CI', 'JOBS')}
    full.update(GOOD_MAC, **env)
    r = subprocess.run(['bash', str(SCRIPT), '--dry-run', *args],
                       capture_output=True, text=True, env=full, check=False)
    self.assertEqual(r.returncode, 0, r.stderr)
    return r.stdout

  def test_default_is_gentle_and_separate(self):
    text = self.plan()
    self.assertIn('caffeinate -i taskpolicy -b nice -n 19 autoninja -C '
                  'out/KoanOfficial -j 4 chrome', text)
    self.assertIn('koan_thin_lto_jobs = 4', text)
    self.assertIn('chrome_pgo_phase = 2', text)
    self.assertIn('above 8', text)
    self.assertNotIn('out/Koan ', text)
    self.assertIn('install      never', text)
    self.assertNotIn('FAIL', text)

  def test_away(self):
    text = self.plan('--away', KOAN_FAKE_AVAIL_GB='12')
    self.assertIn('nice -n 19 autoninja -C out/KoanOfficial -j 8 chrome', text)
    self.assertNotIn('taskpolicy', text)
    self.assertIn('above 16', text)

  def test_signing_order_and_switches(self):
    text = self.plan()
    self.assertIn('widevine_vmp_sign.sh --if-configured, then '
                  'scripts/mac_sign.sh', text)
    self.assertIn('sign         skip', self.plan('--no-sign'))
    self.assertIn('sign         skip', self.plan(KOAN_SIGN='0'))
    self.assertIn('chrome_pgo_phase = 0', self.plan(KOAN_PGO='0'))
    self.assertIn('sync         skip', self.plan('--no-sync'))

  def test_short_disk_fails_the_check(self):
    self.assertIn('FAIL  Disk: 40 GB free, a first official build',
                  self.plan(KOAN_FAKE_DISK_GB='40'))

  def test_resume_needs_less_disk(self):
    text = self.plan(KOAN_FAKE_DISK_GB='40', KOAN_FAKE_OUT_GB='35')
    self.assertIn('Disk: 40 GB free; the rest of the official build needs '
                  'about 35 GB', text)
    self.assertNotIn('FAIL  Disk: 40 GB free,', text)
    self.assertIn('resumes the out dir (35 GB)', text)

  def test_refuses_to_build_off_a_mac(self):
    if sys.platform == 'darwin':
      self.skipTest('on a Mac it would build')
    r = subprocess.run(['bash', str(SCRIPT), '--check'],
                       capture_output=True, text=True, check=False)
    self.assertEqual(r.returncode, 2, r.stderr)


class ArgsFileTest(unittest.TestCase):
  """build/koan-mac-official.gn says what the design says."""

  def args(self):
    values = {}
    for line in ARGS.read_text().splitlines():
      m = re.match(r'^(\w+) = (.+)$', line)
      if m:
        self.assertNotIn(m.group(1), values, f'{m.group(1)} set twice')
        values[m.group(1)] = m.group(2)
    return values

  def test_official_unbranded_no_symbols(self):
    a = self.args()
    self.assertEqual(a['is_official_build'], 'true')
    self.assertEqual(a['is_chrome_branded'], 'false')
    self.assertEqual(a['is_debug'], 'false')
    self.assertEqual(a['dcheck_always_on'], 'false')
    self.assertEqual(a['is_component_build'], 'false')
    self.assertEqual(a['use_thin_lto'], 'true')
    self.assertEqual(a['target_cpu'], '"arm64"')
    for level in ('symbol_level', 'blink_symbol_level', 'v8_symbol_level'):
      self.assertEqual(a[level], '0', level)
    self.assertEqual(a['enable_dsyms'], 'false')

  def test_machine_lines_are_left_to_the_script(self):
    # The script appends these; set here too, GN would see them twice.
    a = self.args()
    for name in ('chrome_pgo_phase', 'koan_thin_lto_jobs',
                 'angle_metal_toolchain_dir', 'concurrent_links'):
      self.assertNotIn(name, a)

  def test_widevine_matches_the_everyday_build(self):
    a = self.args()
    everyday = (ROOT / 'build' / 'koan-mac.gn').read_text()
    for name in ('enable_widevine', 'enable_cdm_host_verification',
                 'proprietary_codecs', 'ffmpeg_branding',
                 'disable_fieldtrial_testing_config'):
      self.assertIn(f'{name} = {a[name]}', everyday, name)

  def test_thin_lto_cap_is_applied_to_the_framework(self):
    build = (ROOT / 'src' / 'browser' / 'BUILD.gn').read_text()
    self.assertIn('all_dependent_configs = [ "//koan/build:thin_lto_jobs" ]',
                  build)
    patch = (ROOT / 'patches' / 'chrome' / 'browser' / 'BUILD.gn.patch').read_text()
    self.assertIn('+    "//koan/browser:feature_overrides",', patch)


class FlockRunTest(unittest.TestCase):

  def setUp(self):
    self.tmp = tempfile.TemporaryDirectory()
    self.addCleanup(self.tmp.cleanup)
    self.lock = os.path.join(self.tmp.name, 'sub', 'e2e.lock')

  def run_locked(self, *cmd, **env):
    full = dict(os.environ, **env)
    return subprocess.run([sys.executable, '-I', str(FLOCK_RUN), self.lock,
                           '--', *cmd], capture_output=True, text=True,
                          env=full, check=False, timeout=60)

  def test_command_holds_the_lock(self):
    # The child sees the lock taken (by its own fd 9) from another open file.
    probe = ('import fcntl,os,sys\n'
             'fd = os.open(sys.argv[1], os.O_RDWR)\n'
             'try:\n'
             '  fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)\n'
             '  print("free")\n'
             'except BlockingIOError:\n'
             '  print("held")\n'
             'os.fstat(9)\n')
    r = self.run_locked(sys.executable, '-I', '-c', probe, self.lock)
    self.assertEqual(r.returncode, 0, r.stderr)
    self.assertEqual(r.stdout.strip(), 'held')

  def test_busy_lock_without_waiting(self):
    os.makedirs(os.path.dirname(self.lock))
    with open(self.lock, 'w') as f:
      fcntl.flock(f, fcntl.LOCK_EX)
      r = self.run_locked('true', KOAN_LOCK_WAIT='0')
    self.assertEqual(r.returncode, 75)
    self.assertIn('held by another', r.stderr)

  def test_usage(self):
    r = subprocess.run([sys.executable, '-I', str(FLOCK_RUN), self.lock],
                       capture_output=True, text=True, check=False)
    self.assertEqual(r.returncode, 2)


if __name__ == '__main__':
  unittest.main()
