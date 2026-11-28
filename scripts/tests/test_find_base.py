#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/ci/find_base.sh, against a fake `gh`.

The fake `gh` on PATH answers `gh api` from a recorded list of runs and their
artifacts the way the GitHub API does: the runs of a workflow, newest
first, narrowed by ?branch= and ?event=, a page of ?per_page= at ?page=; a
run's artifacts; and a tag (git/ref/tags/<name>, and an annotated tag's
object, git/tags/<sha>), or a 404. --jq goes through the real jq. Skipped
without bash or jq.
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
if '/git/ref/tags/' in url.path or '/git/tags/' in url.path:
  table = data['tags'] if '/git/ref/' in url.path else data['tag_objects']
  if parts[-1] not in table:
    sys.stderr.write('gh: Not Found (HTTP 404)\n')
    sys.exit(1)
  body = {'object': table[parts[-1]]}
elif parts[-1] == 'artifacts':
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
MAC = ['koan-mac-source.tar.zst', 'koan-mac-out-final.tar.zst']


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
    self.tags = {}
    self.tag_objects = {}
    self.clock = 0

  def add_run(self, event='schedule', branch='develop', conclusion='success',
              artifacts=(), workflow='linux.yml', sha=None):
    self.clock += 1
    run_id = 1000 + len(self.runs)
    self.runs.append({'id': run_id, 'workflow': workflow, 'event': event,
                      'head_branch': branch, 'conclusion': conclusion,
                      'head_sha': sha or f'{run_id:040x}',
                      'created_at': _time(self.clock)})
    self.artifacts[str(run_id)] = [{'name': n, 'expired': False}
                                   for n in artifacts]
    return run_id

  def add_release(self, tag='v0.0.3-alpha.1', event='push', annotated=False,
                  **kwargs):
    """A release.yml run of `tag`, which points at the run's commit."""
    sha = f'{0xabc000 + len(self.runs):040x}'
    if annotated:
      tag_sha = f'{0xdef000 + len(self.runs):040x}'
      self.tags[tag] = {'type': 'tag', 'sha': tag_sha}
      self.tag_objects[tag_sha] = {'type': 'commit', 'sha': sha}
    else:
      self.tags[tag] = {'type': 'commit', 'sha': sha}
    branch = kwargs.pop('branch', tag)
    return self.add_run(event=event, branch=branch, workflow='release.yml',
                        sha=sha, **kwargs)

  def find(self, run='0', workflow='linux.yml', prefix='koan-linux', **extra):
    data = self.tmp / 'data.json'
    data.write_text(json.dumps({'runs': self.runs,
                                'artifacts': self.artifacts,
                                'tags': self.tags,
                                'tag_objects': self.tag_objects}))
    self.log = self.tmp / 'gh.log'
    self.log.write_text('')
    env = dict(os.environ, FAKE_GH_DATA=str(data), FAKE_GH_LOG=str(self.log),
               PATH=f'{self.tmp / "bin"}{os.pathsep}{os.environ["PATH"]}',
               GH_REPO='owner/repo', RUN=run, BRANCH='develop')
    env.pop('GITHUB_OUTPUT', None)
    env.pop('RELEASE_WORKFLOW', None)
    env.update(extra)
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
    # Only develop's schedule and workflow_dispatch runs of linux.yml, and
    # release.yml's push and workflow_dispatch runs, were asked for.
    lists = [p for p in self.log.read_text().split() if '/workflows/' in p]
    self.assertTrue(lists)
    for path in lists:
      if '/workflows/release.yml/' in path:
        self.assertRegex(path, r'event=(push|workflow_dispatch)&')
      else:
        self.assertIn('/workflows/linux.yml/', path)
        self.assertIn('branch=develop', path)
        self.assertRegex(path, r'event=(schedule|workflow_dispatch)&')

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

  def test_newest_of_nightlies_and_releases(self):
    # A release after the last nightly is the base (it may be the first
    # build on a new Chromium), and the next nightly after it is too.
    self.add_run(artifacts=BOTH)
    release = self.add_release(artifacts=BOTH)
    self.assertEqual(self.find(), f'base_run={release}')
    nightly = self.add_run(artifacts=BOTH)
    self.assertEqual(self.find(), f'base_run={nightly}')
    # The same for the Mac's: release.yml's runs are shared.
    mac = self.add_release(tag='v0.0.3-alpha.2', artifacts=MAC + BOTH)
    self.assertEqual(self.find(workflow='macos.yml', prefix='koan-mac'),
                     f'base_run={mac}')

  def test_release_dispatch_from_develop_or_the_tag(self):
    older = self.add_release(tag='v1.0.0', event='workflow_dispatch',
                             annotated=True, artifacts=BOTH)
    self.assertEqual(self.find(), f'base_run={older}')
    # From develop: develop's workflow files built the tag.
    develop = self.add_release(tag='v1.0.1', event='workflow_dispatch',
                               branch='develop', artifacts=BOTH)
    self.assertEqual(self.find(), f'base_run={develop}')

  def test_never_an_untrusted_release_run(self):
    base = self.add_run(artifacts=BOTH)
    # A dispatch from a branch, even one named like a tag: its own
    # workflow files ran.
    self.add_run(workflow='release.yml', event='workflow_dispatch',
                 branch='feature', artifacts=BOTH)
    self.add_run(workflow='release.yml', event='workflow_dispatch',
                 branch='v9.9.9', artifacts=BOTH)
    # A tag whose commit isn't the run's (moved, or a branch of its name).
    moved = self.add_release(tag='v2.0.0', artifacts=BOTH)
    self.tags['v2.0.0'] = {'type': 'commit', 'sha': 'f' * 40}
    # Not a tag name release.yml takes, other events, failures, the wrong
    # platform's archives.
    self.add_release(tag='nightly', artifacts=BOTH)
    self.add_release(tag='v2.0.1', event='pull_request', artifacts=BOTH)
    self.add_release(tag='v2.0.2', event='schedule', artifacts=BOTH)
    self.add_release(tag='v2.0.3', conclusion='failure', artifacts=BOTH)
    self.add_release(tag='v2.0.4', conclusion=None, artifacts=BOTH)
    mac_only = self.add_release(tag='v2.0.5', artifacts=MAC)
    self.assertEqual(self.find(), f'base_run={base}')
    self.assertEqual(self.find(workflow='macos.yml', prefix='koan-mac'),
                     f'base_run={mac_only}')
    del self.tags['v2.0.5']  # A deleted tag: no base either.
    self.assertEqual(self.find(workflow='macos.yml', prefix='koan-mac'),
                     'base_run=')
    self.assertNotEqual(self.find(), f'base_run={moved}')

  def test_this_release_run_is_not_its_own_base(self):
    nightly = self.add_run(artifacts=BOTH)
    this = self.add_release(artifacts=BOTH)
    self.assertEqual(self.find(run=str(this)), f'base_run={nightly}')

  def test_releases_can_be_left_out(self):
    nightly = self.add_run(artifacts=BOTH)
    self.add_release(artifacts=BOTH)
    self.assertEqual(self.find(RELEASE_WORKFLOW=''), f'base_run={nightly}')
    self.assertNotIn('release.yml', self.log.read_text())


if __name__ == '__main__':
  unittest.main()
