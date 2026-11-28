#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/ci/prune_bases.sh, against a fake `gh`.

The fake `gh` on PATH answers `gh api` from a recorded set of runs and
artifacts the way the GitHub API does: the artifacts of a name
(actions/artifacts?name=), a run (actions/runs/<id>), a run's artifacts,
the runs of a workflow (narrowed by ?branch= and ?event=, as
scripts/ci/find_base.sh asks), a tag (git/ref/tags/<name>), and DELETE of
an artifact, which removes it from the record. --jq goes through the real jq. Skipped without bash or jq.

#569: develop run 46026497864's cleanup deleted the archives of 46019353569
while pull request #514's build, which had picked it as its base, was still
fetching; its build 1 found no out-final and built from scratch.
"""

import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
CI = ROOT / 'scripts' / 'ci'

FAKE_GH = r'''#!/usr/bin/env python3
import json, os, subprocess, sys, urllib.parse
args = sys.argv[1:]
assert args[0] == 'api', args
args = args[1:]
method = 'GET'
if args[0] == '-X':
  method, args = args[1], args[2:]
if args and args[0] == '--paginate':
  args = args[1:]
path = args[0]
jq = args[args.index('--jq') + 1] if '--jq' in args else None
data_file = os.environ['FAKE_GH_DATA']
data = json.load(open(data_file))
with open(os.environ['FAKE_GH_LOG'], 'a') as log:
  log.write(f'{method} {path}\n')
fail = os.environ.get('FAKE_GH_FAIL')
if fail:
  # FAKE_GH_FAIL=<part of the path>:<times>: that many calls fail like a
  # GitHub outage, then they work ('always': never).
  part, times = fail.rsplit(':', 1)
  if part in path:
    count_file = os.environ['FAKE_GH_DATA'] + '.failed'
    failed = int(open(count_file).read()) if os.path.exists(count_file) else 0
    if times == 'always' or failed < int(times):
      open(count_file, 'w').write(str(failed + 1))
      sys.stderr.write('gh: HTTP 502: Server Error (https://api.github.com/'
                       + path + ')\n')
      sys.exit(1)
url = urllib.parse.urlsplit(path)
query = dict(urllib.parse.parse_qsl(url.query))
parts = url.path.split('/')
runs = {str(r['id']): r for r in data['runs']}
if '/git/ref/tags/' in url.path:
  if parts[-1] not in data.get('tags', {}):
    sys.stderr.write('gh: Not Found (HTTP 404)\n')
    sys.exit(1)
  body = {'object': {'type': 'commit', 'sha': data['tags'][parts[-1]]}}
  out = subprocess.run(['jq', '-r', jq], input=json.dumps(body), text=True,
                       capture_output=True, check=True).stdout
  sys.stdout.write(out)
  sys.exit(0)
if method == 'DELETE':
  data['artifacts'] = [a for a in data['artifacts'] if str(a['id']) != parts[-1]]
  json.dump(data, open(data_file, 'w'))
  sys.exit(0)
if parts[-1] == 'artifacts' and parts[-3] == 'runs':
  body = {'artifacts': [a for a in data['artifacts']
                        if str(a['run']) == parts[-2]]}
elif parts[-1] == 'artifacts':
  body = {'artifacts': [
      dict(a, workflow_run={'id': a['run'],
                            'head_branch': runs[str(a['run'])]['head_branch']})
      for a in data['artifacts'] if a['name'] == query['name']]}
elif parts[-2] == 'runs':
  body = runs[parts[-1]]
else:
  found = [r for r in data['runs'] if r['path'].endswith('/' + parts[-2])
           and query.get('branch', r['head_branch']) == r['head_branch']
           and query.get('event', r['event']) == r['event']]
  found.sort(key=lambda r: r['created_at'], reverse=True)
  per_page = int(query.get('per_page', 30))
  page = int(query.get('page', 1))
  body = {'workflow_runs': found[(page - 1) * per_page:page * per_page]}
out = subprocess.run(['jq', '-r', jq], input=json.dumps(body), text=True,
                     capture_output=True, check=True).stdout
sys.stdout.write(out)
'''

SOURCE = 'koan-linux-source.tar.zst'
FINAL = 'koan-linux-out-final.tar.zst'
BOTH = (SOURCE, FINAL)
HOUR = 3600
NOW = 1_800_000_000  # The clock the scripts are given.


def _iso(t):
  return subprocess.run(['date', '-u', '-d', f'@{t}', '+%Y-%m-%dT%H:%M:%SZ'],
                        capture_output=True, text=True, check=True).stdout.strip()


@unittest.skipUnless(shutil.which('bash') and shutil.which('jq'),
                     'needs bash and jq')
class FakeGhTest(unittest.TestCase):

  def setUp(self):
    self.tmp = pathlib.Path(tempfile.mkdtemp())
    self.addCleanup(shutil.rmtree, self.tmp)
    bin_dir = self.tmp / 'bin'
    bin_dir.mkdir()
    gh = bin_dir / 'gh'
    gh.write_text(FAKE_GH)
    gh.chmod(0o755)
    self.runs = []
    self.artifacts = []
    self.tags = {}
    self.data = self.tmp / 'data.json'
    self.log = self.tmp / 'gh.log'

  def add_run(self, ended, artifacts=BOTH, conclusion='success',
              status='completed', branch='develop', event='schedule',
              workflow='linux.yml', expired=()):
    """A run that ended `ended` seconds before NOW."""
    run_id = 46_000_000_000 + len(self.runs)
    self.runs.append({'id': run_id, 'status': status,
                      'conclusion': conclusion if status == 'completed' else None,
                      'updated_at': _iso(NOW - ended),
                      'created_at': _iso(NOW - ended - 10 * HOUR),
                      'head_branch': branch, 'event': event,
                      'head_sha': f'{run_id:040x}',
                      'path': f'.github/workflows/{workflow}'})
    for name in artifacts:
      self.artifacts.append({'id': 900_000 + len(self.artifacts),
                             'run': run_id, 'name': name,
                             'expired': name in expired})
    return run_id

  def add_release(self, ended, tag, **kwargs):
    """A release.yml run of `tag` (pushed), which points at its commit."""
    run_id = self.add_run(ended, branch=tag, event='push',
                          workflow='release.yml', **kwargs)
    self.tags[tag] = f'{run_id:040x}'
    return run_id

  def env(self, **extra):
    self.data.write_text(json.dumps({'runs': self.runs,
                                     'artifacts': self.artifacts,
                                     'tags': self.tags}))
    self.log.write_text('')
    (self.tmp / 'data.json.failed').unlink(missing_ok=True)
    env = dict(os.environ, FAKE_GH_DATA=str(self.data),
               FAKE_GH_LOG=str(self.log), GH_REPO='owner/repo',
               BRANCH='develop', NOW=str(NOW), GH_API_DELAY='0',
               PATH=f'{self.tmp / "bin"}{os.pathsep}{os.environ["PATH"]}')
    for key in ('GITHUB_OUTPUT', 'GITHUB_STEP_SUMMARY', 'GRACE_HOURS',
                'FAKE_GH_FAIL', 'GH_API_TRIES'):
      env.pop(key, None)
    env.update(extra)
    return env

  def recorded(self):
    """Run id -> the archive names add_run gave it."""
    runs = {}
    for a in self.artifacts:
      runs.setdefault(a['run'], set()).add(a['name'])
    return runs

  def left(self):
    """Run id -> the archive names it still has."""
    left = {}
    for a in json.loads(self.data.read_text())['artifacts']:
      left.setdefault(a['run'], set()).add(a['name'])
    return left


class PruneBasesTest(FakeGhTest):

  def prune(self, run, **extra):
    return self.run_prune(run, **extra).stdout

  def run_prune(self, run, **extra):
    return subprocess.run(
        ['bash', str(CI / 'prune_bases.sh'), 'linux.yml', 'koan-linux'],
        env=self.env(RUN=str(run), **extra), capture_output=True, text=True,
        check=True)

  def test_previous_base_survives_this_run(self):
    # #569: the base a pull request's build picked minutes ago stays.
    previous = self.add_run(ended=20 * HOUR)
    this = self.add_run(ended=0)
    self.prune(this)
    self.assertEqual(self.left()[previous], set(BOTH))
    self.assertNotIn('DELETE', self.log.read_text())

  def test_base_superseded_long_ago_goes(self):
    old = self.add_run(ended=40 * HOUR)
    previous = self.add_run(ended=20 * HOUR)  # Newest base for 20 h.
    this = self.add_run(ended=0)
    out = self.prune(this)
    left = self.left()
    self.assertNotIn(old, left)
    self.assertEqual(left[previous], set(BOTH))
    self.assertEqual(left[this], set(BOTH))
    self.assertIn(f'deleting the archives of run {old}', out)

  def test_bases_of_a_busy_day_stay_12_hours(self):
    # Develop builds hours apart: each base stays until a newer one has
    # existed for 12 h, whatever came after it.
    older = self.add_run(ended=30 * HOUR)  # Superseded 13 h ago: goes.
    old = self.add_run(ended=13 * HOUR)  # Superseded 5 h ago: stays.
    previous = self.add_run(ended=5 * HOUR)
    this = self.add_run(ended=0)
    self.prune(this)
    self.assertEqual(sorted(self.left()), [old, previous, this])

  def test_grace_is_settable(self):
    previous = self.add_run(ended=5 * HOUR)
    middle = self.add_run(ended=2 * HOUR)
    this = self.add_run(ended=0)
    subprocess.run(
        ['bash', str(CI / 'prune_bases.sh'), 'linux.yml', 'koan-linux'],
        env=self.env(RUN=str(this), GRACE_HOURS='1'), check=True,
        capture_output=True)
    self.assertEqual(sorted(self.left()), [middle, this])
    self.assertNotIn(previous, self.left())

  def test_no_bases_go_at_once_running_ones_stay(self):
    base = self.add_run(ended=30 * HOUR)
    failed = self.add_run(ended=29 * HOUR, artifacts=(SOURCE,),
                          conclusion='failure')
    no_final = self.add_run(ended=28 * HOUR, artifacts=(SOURCE,))
    expired = self.add_run(ended=27 * HOUR, expired=(FINAL,))
    running = self.add_run(ended=0, artifacts=(SOURCE,), status='in_progress')
    this = self.add_run(ended=0)
    self.prune(this)
    left = self.left()
    for run in (failed, no_final):
      self.assertNotIn(run, left)
    # The expired out-final isn't deleted (it is gone already), its source is.
    self.assertEqual(left[expired], {FINAL})
    self.assertEqual(left[running], {SOURCE})
    # The only base, and newest until now: it stays.
    self.assertEqual(left[base], set(BOTH))

  def test_releases_and_nightlies_supersede_each_other(self):
    old = self.add_run(ended=60 * HOUR)
    release = self.add_release(ended=40 * HOUR, tag='v0.0.3')
    nightly = self.add_run(ended=20 * HOUR)
    this = self.add_release(ended=0, tag='v0.0.4')  # A release's cleanup.
    out = self.prune(this)
    left = self.left()
    self.assertNotIn(old, left)
    self.assertNotIn(release, left)
    self.assertIn(f'deleting the archives of run {release}', out)
    self.assertEqual(left[nightly], set(BOTH))

  def test_a_release_that_is_no_base_keeps_its_archives(self):
    failed = self.add_release(ended=40 * HOUR, tag='v0.0.3',
                              conclusion='failure', artifacts=(SOURCE,))
    this = self.add_run(ended=0)
    self.prune(this)
    self.assertEqual(self.left()[failed], {SOURCE})

  def test_untrusted_release_runs_are_not_touched(self):
    # A branch named like a tag, and a tag that moved: not this script's.
    branch = self.add_run(ended=40 * HOUR, branch='v9', event='workflow_dispatch',
                          workflow='release.yml')
    moved = self.add_release(ended=40 * HOUR, tag='v0.0.3')
    self.tags['v0.0.3'] = 'f' * 40
    old = self.add_run(ended=40 * HOUR)
    self.add_run(ended=20 * HOUR)
    this = self.add_run(ended=0)
    self.prune(this)
    left = self.left()
    self.assertNotIn(old, left)
    self.assertEqual(left[branch], set(BOTH))
    self.assertEqual(left[moved], set(BOTH))

  def test_only_this_workflow_and_branch_and_older_runs(self):
    feature = self.add_run(ended=40 * HOUR, branch='feature',
                           event='pull_request', artifacts=(SOURCE,))
    release = self.add_run(ended=40 * HOUR, workflow='release.yml',
                           conclusion='failure', artifacts=(SOURCE,))
    old = self.add_run(ended=40 * HOUR)
    self.add_run(ended=20 * HOUR)
    this = self.add_run(ended=1 * HOUR)
    newer = self.add_run(ended=0, conclusion='failure', artifacts=(SOURCE,))
    self.prune(this)
    left = self.left()
    self.assertNotIn(old, left)
    for run in (feature, release, newer):
      self.assertIn(run, left)


  # #589: an API error must never be read as "no base". A failed out-final
  # list made every base look like it had only its source, so "no base:
  # delete it".

  def test_a_failing_archive_list_deletes_nothing(self):
    self.add_run(ended=60 * HOUR)  # Would go: superseded 40 h ago.
    self.add_run(ended=40 * HOUR)
    self.add_run(ended=20 * HOUR)
    this = self.add_run(ended=0)
    before = self.recorded()
    for part in ('name=koan-linux-out-final', 'name=koan-linux-source'):
      with self.subTest(part=part):
        result = self.run_prune(this, FAKE_GH_FAIL=f'{part}:always')
        self.assertEqual(self.left(), before)
        self.assertNotIn('DELETE', self.log.read_text())
        self.assertIn(f'::warning::could not look up the koan-linux-', result.stdout)
        self.assertIn('deleting nothing more', result.stdout)
        self.assertIn('GitHub said: gh: HTTP 502', result.stderr)

  def test_a_failing_run_lookup_deletes_nothing_more(self):
    old = self.add_run(ended=60 * HOUR)
    self.add_run(ended=40 * HOUR)
    newest = self.add_run(ended=20 * HOUR)
    this = self.add_run(ended=0)
    before = self.recorded()
    result = self.run_prune(this, FAKE_GH_FAIL=f'actions/runs/{newest}:always')
    self.assertEqual(self.left(), before)
    self.assertIn(f'::warning::could not look up run {newest}', result.stdout)
    self.assertNotIn('deleting the archives', result.stdout)
    self.assertIn(old, self.left())

  def test_a_call_that_fails_once_is_retried(self):
    old = self.add_run(ended=40 * HOUR)
    previous = self.add_run(ended=20 * HOUR)
    this = self.add_run(ended=0)
    result = self.run_prune(this, FAKE_GH_FAIL='name=koan-linux-out-final:1')
    self.assertIn('failed (try 1 of 4', result.stderr)
    left = self.left()
    self.assertNotIn(old, left)
    self.assertEqual(left[previous], set(BOTH))


if __name__ == '__main__':
  unittest.main()
