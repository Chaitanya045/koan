#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/ci/find_base.sh, against a fake `gh`.

The fake `gh` on PATH answers `gh api` from a recorded list of runs and their
artifacts the way the GitHub API does: the runs of the workflow, newest
first, narrowed by ?branch= and ?event=, a page of ?per_page= at ?page=; and
a run's artifacts. --jq goes through the real jq. Skipped without bash or jq.
"""

import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts' / 'ci' / 'find_base.sh'

FAKE_GH = r'''#!/usr/bin/env python3
import json, os, subprocess, sys, urllib.parse
args = sys.argv[1:]
assert args[0] == 'api', args
path, jq = args[1], args[args.index('--jq') + 1]
data = json.load(open(os.environ['FAKE_GH_DATA']))
with open(os.environ['FAKE_GH_LOG'], 'a') as log:
  log.write(path + '\n')
url = urllib.parse.urlsplit(path)
query = dict(urllib.parse.parse_qsl(url.query))
parts = url.path.split('/')
if parts[-1] == 'artifacts':
  body = {'artifacts': data['artifacts'].get(parts[-2], [])}
else:
  runs = [r for r in data['runs'] if r['workflow'] == parts[-2]
          and query.get('branch', r['head_branch']) == r['head_branch']
          and query.get('event', r['event']) == r['event']]
  runs.sort(key=lambda r: r['created_at'], reverse=True)
  per_page = int(query.get('per_page', 30))
  page = int(query.get('page', 1))
  body = {'total_count': len(runs),
          'workflow_runs': runs[(page - 1) * per_page:page * per_page]}
out = subprocess.run(['jq', '-r', jq], input=json.dumps(body), text=True,
                     capture_output=True, check=True).stdout
sys.stdout.write(out)
'''

BOTH = ['koan-linux-source.tar.zst', 'koan-linux-out-final.tar.zst']


def _time(n):
  """The n-th minute after 2026-11-26T00:00:00Z, as the API writes it."""
  return f'2026-11-26T{n // 60:02d}:{n % 60:02d}:00Z'


@unittest.skipUnless(shutil.which('bash') and shutil.which('jq'),
                     'needs bash and jq')
class FindBaseTest(unittest.TestCase):

  def setUp(self):
    self.tmp = pathlib.Path(tempfile.mkdtemp())
    self.addCleanup(shutil.rmtree, self.tmp)
    bin_dir = self.tmp / 'bin'
    bin_dir.mkdir()
    gh = bin_dir / 'gh'
    gh.write_text(FAKE_GH)
    gh.chmod(0o755)
    self.runs = []
    self.artifacts = {}
    self.clock = 0

  def add_run(self, event='schedule', branch='develop', conclusion='success',
              artifacts=(), workflow='linux.yml'):
    self.clock += 1
    run_id = 1000 + len(self.runs)
    self.runs.append({'id': run_id, 'workflow': workflow, 'event': event,
                      'head_branch': branch, 'conclusion': conclusion,
                      'created_at': _time(self.clock)})
    self.artifacts[str(run_id)] = [{'name': n, 'expired': False}
                                   for n in artifacts]
    return run_id

  def find(self, run='0', workflow='linux.yml', prefix='koan-linux'):
    data = self.tmp / 'data.json'
    data.write_text(json.dumps({'runs': self.runs,
                                'artifacts': self.artifacts}))
    self.log = self.tmp / 'gh.log'
    self.log.write_text('')
    env = dict(os.environ, FAKE_GH_DATA=str(data), FAKE_GH_LOG=str(self.log),
               PATH=f'{self.tmp / "bin"}{os.pathsep}{os.environ["PATH"]}',
               GH_REPO='owner/repo', RUN=run, BRANCH='develop')
    env.pop('GITHUB_OUTPUT', None)
    out = subprocess.run(['bash', str(SCRIPT), workflow, prefix], env=env,
                         capture_output=True, text=True, check=True).stdout
    return out.strip().splitlines()[-1]

  def test_base_behind_more_than_50_pull_request_runs(self):
    # #537: a dozen labelled pull requests made more than 50 runs after the
    # last develop run, and the newest 50 runs held no base.
    base = self.add_run(artifacts=BOTH)
    for _ in range(120):
      self.add_run(event='pull_request', branch='feature',
                   artifacts=['koan-linux-source.tar.zst'])
    self.assertEqual(self.find(), f'base_run={base}')
    # Only develop's schedule and workflow_dispatch runs were asked for.
    lists = [p for p in self.log.read_text().split() if '/workflows/' in p]
    self.assertTrue(lists)
    for path in lists:
      self.assertIn('branch=develop', path)
      self.assertRegex(path, r'event=(schedule|workflow_dispatch)')

  def test_newest_of_both_events_with_both_archives(self):
    self.add_run(artifacts=BOTH)
    dispatch = self.add_run(event='workflow_dispatch', artifacts=BOTH)
    self.add_run(artifacts=['koan-linux-source.tar.zst'])  # no out-final
    self.add_run(conclusion='failure', artifacts=BOTH)
    self.add_run(conclusion=None, artifacts=BOTH)  # still running
    self.assertEqual(self.find(), f'base_run={dispatch}')

  def test_never_this_run_nor_other_branches_or_events(self):
    older = self.add_run(artifacts=BOTH)
    self.add_run(event='pull_request', branch='develop', artifacts=BOTH)
    self.add_run(event='push', artifacts=BOTH)
    self.add_run(event='workflow_dispatch', branch='feature', artifacts=BOTH)
    self.add_run(workflow='macos.yml', artifacts=BOTH)
    this = self.add_run(artifacts=BOTH)
    self.assertEqual(self.find(run=str(this)), f'base_run={older}')

  def test_expired_archive_is_no_base(self):
    run = self.add_run(artifacts=BOTH)
    self.artifacts[str(run)][1]['expired'] = True
    self.assertEqual(self.find(), 'base_run=')

  def test_pages_past_the_first_hundred(self):
    base = self.add_run(artifacts=BOTH)
    for _ in range(150):
      self.add_run(conclusion='failure')
    self.assertEqual(self.find(), f'base_run={base}')

  def test_no_runs(self):
    self.assertEqual(self.find(), 'base_run=')


if __name__ == '__main__':
  unittest.main()
