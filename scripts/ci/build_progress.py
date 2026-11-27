#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Checks that a chained CI build job resumes where the previous one stopped.

The build slices (scripts/ci/mac_build_slice.sh, linux_build_slice.sh) hand
the out directory on from job to job. If something makes siso think the
previous job's work is stale, the next job quietly redoes it, and a full
build never ends (issue #534). These commands catch that:

  build_progress.py record <build.log> <state file>
      At the end of a job that stopped at the deadline: writes the steps
      siso had left ("N remaining", else total - done of the last
      [done/total] line) and $GITHUB_RUN_ID to the state file, which lives
      in the out directory and so travels to the next job.

  build_progress.py check <build.log> <state file>
      A few minutes into the next job: compares siso's current total with
      what the previous job left. Exit 0: fine, or nothing to compare (no
      state, or a state from another run, e.g. an incremental build's base).
      Exit 1: the job has far more to do than the previous job left, so it
      is rebuilding finished work. Exit 2: no progress line yet.

A healthy resume settles within two minutes at or below what the previous
job left (run 45816128161: build-02's total was 49,796 against build-01's
52,255 remaining); the broken jobs settled at 1.7-1.9 times it.
"""

import argparse
import os
import pathlib
import re
import sys

PROGRESS = re.compile(rb'^\[(\d+)/(\d+)\]', re.M)
REMAINING = re.compile(rb'(\d+) done, \d+ failed, (\d+) remaining')

# More than this many times what the previous job left, plus SLACK steps,
# is a rebuild of finished work.
RATIO = 1.25
SLACK = 1000


def last_progress(log):
  """The last [done/total] line of a siso log, as (done, total), or None."""
  last = None
  for m in PROGRESS.finditer(log):
    last = m
  return (int(last[1]), int(last[2])) if last else None


def remaining(log):
  """The steps siso had left when it stopped, or None."""
  found = REMAINING.findall(log)
  if found:
    return int(found[-1][1])
  progress = last_progress(log)
  return progress[1] - progress[0] if progress else None


def read_state(text):
  state = {}
  for line in text.splitlines():
    key, sep, value = line.partition('=')
    if sep:
      state[key.strip()] = value.strip()
  return state


def too_many(total, previous):
  return total > previous * RATIO + SLACK


def record(log_path, state_path, run_id):
  left = remaining(pathlib.Path(log_path).read_bytes())
  if left is None:
    print(f'no siso progress in {log_path}; nothing recorded')
    return 0
  pathlib.Path(state_path).write_text(f'run={run_id}\nremaining={left}\n')
  print(f'recorded for the next job: {left} steps left')
  return 0


def check(log_path, state_path, run_id):
  state_file = pathlib.Path(state_path)
  if not state_file.is_file():
    print('resume check: no previous job to compare with')
    return 0
  state = read_state(state_file.read_text())
  if state.get('run') != run_id or not state.get('remaining', '').isdigit():
    print(f'resume check: the out directory is from run {state.get("run")}, '
          'not this one; nothing to compare')
    return 0
  previous = int(state['remaining'])
  progress = last_progress(pathlib.Path(log_path).read_bytes())
  if progress is None:
    return 2
  done, total = progress
  if too_many(total, previous):
    print(f'::error::This build job has {total} steps to do ({done} done '
          f'so far), but the previous job left only {previous}: siso is '
          'rebuilding work an earlier job finished, and the chain would '
          'never end. See siso_explain in the logs artifact for why, and '
          '.agent/notes/macos-ci.md ("Resuming").')
    return 1
  print(f'resume check: {total} steps to do, the previous job left '
        f'{previous}: ok')
  return 0


def main(argv):
  parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
  parser.add_argument('command', choices=('record', 'check'))
  parser.add_argument('log')
  parser.add_argument('state')
  args = parser.parse_args(argv)
  run_id = os.environ.get('GITHUB_RUN_ID', 'local')
  if args.command == 'record':
    return record(args.log, args.state, run_id)
  return check(args.log, args.state, run_id)


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
