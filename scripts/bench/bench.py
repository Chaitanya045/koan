#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Benchmarks Koan against other browsers. Run via scripts/benchmark.sh.

For every browser, each run with a fresh profile:
- startup: launch -> the benchmark page's load beacon reaches the local
  server (server-side clock, so engines compare fairly); median of N runs;
- memory: total PSS of the browser's process tree with 5 tabs, after 15 s;
- Speedometer 3.1 (Chromium-based browsers only, driven over CDP).

Browsers run off-screen (Chromium's headless Ozone platform with the full
browser UI; Firefox --headless). Prints a Markdown table and writes JSON.
"""

import argparse
import json
import os
import pathlib
import signal
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
PORT = 9690


def chromium_cmd(binary):

  def cmd(profile, urls, extra=()):
    return [
        binary, '--ozone-platform=headless', '--no-first-run',
        '--no-default-browser-check', '--disable-component-update',
        '--window-size=1280,800', f'--user-data-dir={profile}', *extra, *urls
    ]

  return cmd


def firefox_cmd(binary):

  def cmd(profile, urls, extra=()):
    return [binary, '--headless', '--no-remote', '--profile', profile, *urls]

  return cmd


def launch(cmd):
  return subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, start_new_session=True)


def stop(proc):
  try:
    os.killpg(proc.pid, signal.SIGTERM)
    proc.wait(timeout=10)
  except (ProcessLookupError, subprocess.TimeoutExpired):
    try:
      os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
      pass


def descendants(pid):
  children = {}
  for d in pathlib.Path('/proc').iterdir():
    if not d.name.isdigit():
      continue
    try:
      ppid = int((d / 'stat').read_text().rsplit(')', 1)[1].split()[1])
    except (OSError, IndexError, ValueError):
      continue
    children.setdefault(ppid, []).append(int(d.name))
  out, todo = [], [pid]
  while todo:
    p = todo.pop()
    out.append(p)
    todo.extend(children.get(p, []))
  return out


def pss_kib(pids):
  total = 0
  for p in pids:
    try:
      for line in pathlib.Path(f'/proc/{p}/smaps_rollup').read_text().splitlines():
        if line.startswith('Pss:'):
          total += int(line.split()[1])
    except OSError:
      pass
  return total


def wait_mark(marks, run, timeout=60):
  deadline = time.monotonic() + timeout
  while time.monotonic() < deadline:
    if marks.exists():
      for line in marks.read_text().splitlines():
        name, t = line.split()
        if name == run:
          return float(t)
    time.sleep(0.005)
  return None


def bench_startup(name, cmd, marks, runs):
  times = []
  for i in range(runs):
    run = f'{name}-start-{i}'
    with tempfile.TemporaryDirectory() as profile:
      t0 = time.monotonic()
      proc = launch(cmd(profile, [f'http://127.0.0.1:{PORT}/#{run}']))
      t = wait_mark(marks, run)
      stop(proc)
    if t is not None:
      times.append(t - t0)
    time.sleep(1)
  return times


def bench_memory(name, cmd, settle):
  urls = [f'http://127.0.0.1:{PORT}/#{name}-mem-{i}' for i in range(5)]
  with tempfile.TemporaryDirectory() as profile:
    proc = launch(cmd(profile, urls))
    time.sleep(settle)
    kib = pss_kib(descendants(proc.pid))
    stop(proc)
  return kib


def bench_speedometer(cmd):
  with tempfile.TemporaryDirectory() as profile:
    proc = launch(cmd(profile, ['about:blank'], ['--remote-debugging-port=9691']))
    time.sleep(3)
    try:
      out = subprocess.run(
          ['node', str(ROOT / 'scripts/bench/speedometer.mjs'), '9691'],
          capture_output=True, text=True, timeout=900)
      score = json.loads(out.stdout.strip().splitlines()[-1])
    except (subprocess.TimeoutExpired, json.JSONDecodeError, IndexError):
      score = None
    stop(proc)
  return score


def main():
  ap = argparse.ArgumentParser()
  ap.add_argument('--runs', type=int, default=7)
  ap.add_argument('--settle', type=int, default=15)
  ap.add_argument('--no-speedometer', action='store_true')
  ap.add_argument('--koan', default=str(ROOT / 'chromium/src/out/Release/chrome'))
  ap.add_argument('--json', default=None)
  args = ap.parse_args()

  browsers = [('Koan', 'chromium', args.koan)]
  for label, kind, binary in (('Chromium (Arch)', 'chromium', '/usr/bin/chromium'),
                              ('Firefox (Zen engine)', 'firefox', '/usr/bin/firefox')):
    if pathlib.Path(binary).exists():
      browsers.append((label, kind, binary))

  results = []
  with tempfile.TemporaryDirectory() as tmp:
    marks = pathlib.Path(tmp) / 'marks'
    server = subprocess.Popen(
        [sys.executable, '-I', str(ROOT / 'scripts/bench/server.py'), str(PORT),
         str(marks)])
    time.sleep(1)
    try:
      for label, kind, binary in browsers:
        cmd = chromium_cmd(binary) if kind == 'chromium' else firefox_cmd(binary)
        key = label.split()[0].lower()
        print(f'== {label}', file=sys.stderr)
        startup = bench_startup(key, cmd, marks, args.runs)
        memory = bench_memory(key, cmd, args.settle)
        speedo = (bench_speedometer(cmd)
                  if kind == 'chromium' and not args.no_speedometer else None)
        results.append({
            'browser': label,
            'binary': binary,
            'startup_s': startup,
            'startup_median_s': statistics.median(startup) if startup else None,
            'memory_5_tabs_mib': round(memory / 1024),
            'speedometer': speedo,
        })
    finally:
      server.terminate()

  print('| Browser | Startup to first load (median) | Memory, 5 tabs (PSS) | Speedometer 3.1 |')
  print('|---|---|---|---|')
  for r in results:
    st = f"{r['startup_median_s'] * 1000:.0f} ms" if r['startup_median_s'] else 'n/a'
    sp = f"{r['speedometer']:.1f}" if r['speedometer'] else 'n/a'
    print(f"| {r['browser']} | {st} | {r['memory_5_tabs_mib']} MiB | {sp} |")
  if args.json:
    pathlib.Path(args.json).write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
  main()
