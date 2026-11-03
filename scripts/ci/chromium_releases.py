#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Watches Chromium's Mac releases for Koan (.github/workflows/chromium-releases.yml).

Koan follows the release branch of one milestone (CHROMIUM_VERSION, see
docs/REBASE.md "Following Chromium releases"). This looks at chromiumdash for
what is newer than the pin:

  - a point release of our milestone, Beta or Stable: bump the pin within days;
  - a newer milestone that has reached Stable: rebase within about 2 weeks.

For each it runs scripts/ci/check_patches.sh against that version, and opens
or updates ONE tracking issue (label `chromium-update`) with the versions and
what the patch check said. When nothing is newer than the pin any more, it
closes the issue. Nothing happens, and nothing is posted, when the versions
and the patches are the same as the last time.

  chromium_releases.py [--dry-run] [--pin VERSION]

--dry-run prints instead of using gh; --pin pretends the pin is VERSION.
"""

import argparse
import datetime
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
RELEASES_URL = ('https://chromiumdash.appspot.com/fetch_releases'
                '?channel=%s&platform=Mac&num=%d')
LABEL = 'chromium-update'
MARKER_RE = re.compile(r'<!-- koan-chromium-update: (\{.*?\}) -->')
VERSION_RE = re.compile(r'^\d+\.\d+\.\d+\.\d+$')

# The marker's fields that say what the issue is about; the rest is the
# outcome of the check.
IDENTITY = ('pin', 'point', 'milestone', 'patches')
FETCH_ATTEMPTS = 4
CHECK_ATTEMPTS = 3

# How long the policy gives us (docs/REBASE.md).
POINT_RELEASE_DAYS = 3
NEW_MILESTONE_DAYS = 14


def version_key(version):
  return tuple(int(part) for part in version.split('.'))


def milestone(version):
  return int(version.split('.')[0])


class Release:

  def __init__(self, version, channel, released_ms, commit=''):
    if not VERSION_RE.match(version):
      raise ValueError('not a Chromium version: %r' % (version,))
    self.version = version
    self.channel = channel
    self.released = datetime.datetime.fromtimestamp(
        released_ms / 1000, datetime.timezone.utc)
    self.commit = commit

  @property
  def milestone(self):
    return milestone(self.version)


def parse_releases(channel, entries):
  """The Releases of one channel from chromiumdash's JSON."""
  return [
      Release(e['version'], channel, e['time'],
              e.get('hashes', {}).get('chromium', '')) for e in entries
  ]


def pick_targets(pin, releases):
  """What the pin should move to: {'point': Release, 'milestone': Release}.

  `point` is the newest release of the pin's milestone, Beta or Stable;
  `milestone` the newest Stable release of a newer milestone. Either is
  missing if there is none.
  """
  pin_milestone = milestone(pin)
  targets = {}
  point = [
      r for r in releases
      if r.milestone == pin_milestone and version_key(r.version) > version_key(pin)
  ]
  if point:
    # Stable wins a tie: the same version is listed once per channel it is in.
    targets['point'] = max(
        point, key=lambda r: (version_key(r.version), r.channel == 'Stable'))
  newer = [
      r for r in releases if r.channel == 'Stable' and r.milestone > pin_milestone
  ]
  if newer:
    targets['milestone'] = max(newer, key=lambda r: version_key(r.version))
  return targets


def parse_check(output):
  """(number of patches, [(status, file)] of those that aren't ok)."""
  total = 0
  bad = []
  for line in output.splitlines():
    match = re.match(r'^(ok|CONFLICT|MISSING|FETCH FAILED)\s+(\S+)$', line)
    if not match:
      continue
    total += 1
    if match.group(1) != 'ok':
      bad.append((match.group(1), match.group(2)))
  return total, bad


def summarize_check(result):
  """One line for a table cell; `result` is (total, bad) or None."""
  if result is None:
    return 'not checked: tag not found (yet?)'
  total, bad = result
  if total == 0:
    return 'not checked: no result'
  if not bad:
    return 'all %d patches apply' % total
  return '**%d of %d patches fail**: %s' % (len(bad), total, ', '.join(
      '`%s` (%s)' % (f.rsplit('/', 1)[-1], status) for status, f in bad))


def due(release, days):
  return (release.released + datetime.timedelta(days=days)).strftime('%Y-%m-%d')


def render_body(pin, targets, checks, state, run_url=''):
  """The tracking issue's body. `checks` maps version -> (total, bad) / None."""
  lines = [
      'Koan is pinned to **%s** (`CHROMIUM_VERSION`). Chrome has newer Mac '
      'releases we should take, for their security fixes:' % pin,
      '',
      '| What | Version | Channel | Released | Due | Patch check |',
      '|---|---|---|---|---|---|',
  ]
  rows = [('point', 'Point release of M%d' % milestone(pin), POINT_RELEASE_DAYS),
          ('milestone', 'New milestone at Stable', NEW_MILESTONE_DAYS)]
  for key, what, days in rows:
    release = targets.get(key)
    if release is None:
      continue
    lines.append('| %s | **%s** | %s | %s | %s | %s |' %
                 (what, release.version, release.channel,
                  release.released.strftime('%Y-%m-%d'), due(release, days),
                  summarize_check(checks.get(release.version))))
  lines += [
      '',
      'How to take them: [docs/REBASE.md](../blob/develop/docs/REBASE.md#following-chromium-releases).',
      'A point release is a pin bump (`scripts/ci/check_patches.sh <version>`, '
      'then the switch of the build checkout). A new milestone is a rebase '
      '(`scripts/update_chromium.sh <version>` and the hook audit). Until a '
      'new milestone is taken, stay on the latest Stable point release of '
      'ours.',
      '',
  ]
  failing = [(v, c[1]) for v, c in checks.items() if c and c[1]]
  if failing:
    lines.append('Patches that need work, by version: ' + '; '.join(
        '%s: %s' % (v, ', '.join('`%s`' % f for _, f in bad))
        for v, bad in failing))
    lines.append('')
  if run_url:
    lines.append('Updated by [this run](%s). This issue is maintained by '
                 'the workflow; it closes itself once the pin has caught up.' %
                 run_url)
  if not state.get('complete'):
    lines.append('A check could not be completed (the tag is not on '
                 'googlesource yet, or it could not be reached); the next '
                 'run tries again.')
    lines.append('')
  lines.append('<!-- koan-chromium-update: %s -->' % json.dumps(state, sort_keys=True))
  return '\n'.join(lines)


def title_for(targets):
  versions = [targets[k].version for k in ('point', 'milestone') if k in targets]
  return 'Chromium update available: ' + ', '.join(versions)


def state_of(pin, targets, patches_tree):
  return {
      'pin': pin,
      'point': targets['point'].version if 'point' in targets else '',
      'milestone': targets['milestone'].version if 'milestone' in targets else '',
      'patches': patches_tree,
  }


def check_complete(result):
  """Is a patch-check result a real answer? Not if the tag wasn't there yet,
  or googlesource failed (no patches counted, or a fetch failed)."""
  if result is None:
    return False
  total, bad = result
  return total > 0 and all(status != 'FETCH FAILED' for status, _ in bad)


def marker_of(state, checks):
  """The issue's hidden state: what it is about, and what the checks said
  (so a later run knows whether to check again)."""
  marker = dict(state)
  marker['checks'] = {
      v: None if c is None else {'total': c[0], 'bad': [list(b) for b in c[1]]}
      for v, c in sorted(checks.items())
  }
  marker['complete'] = bool(checks) and all(
      check_complete(c) for c in checks.values())
  return marker


def same_issue(old, state):
  """Is `old` (a marker) a finished answer about `state`?"""
  return bool(old) and old.get('complete') is True and all(
      old.get(k) == state[k] for k in IDENTITY)


def marker_state(body):
  match = MARKER_RE.search(body or '')
  if not match:
    return None
  try:
    return json.loads(match.group(1))
  except ValueError:
    return None


def fetch_channel(channel):
  """The Mac releases of a channel; retried with a growing pause."""
  request = urllib.request.Request(
      RELEASES_URL % (channel, 40), headers={'User-Agent': 'koan-ci'})
  for attempt in range(FETCH_ATTEMPTS):
    try:
      with urllib.request.urlopen(request, timeout=60) as response:
        return parse_releases(channel, json.load(response))
    except (urllib.error.URLError, OSError, ValueError, KeyError) as e:
      if attempt == FETCH_ATTEMPTS - 1:
        raise
      print('chromiumdash %s: %s; trying again' % (channel, e), file=sys.stderr)
      time.sleep(5 * 4**attempt)


def fetch_releases():
  return fetch_channel('Beta') + fetch_channel('Stable')


def run_check(version):
  """(total, bad) from check_patches.sh; None if the tag doesn't exist (yet);
  (0, []) if googlesource couldn't be reached."""
  with tempfile.TemporaryDirectory() as cache:
    env = dict(os.environ, KOAN_CHROMIUM_FILE_CACHE=cache)
    proc = subprocess.run([str(ROOT / 'scripts/ci/check_patches.sh'), version],
                          capture_output=True, text=True, env=env, check=False)
  if proc.returncode == 2:  # No such revision.
    return None
  return parse_check(proc.stdout)


def check_with_retries(version):
  """run_check, again after a pause while the answer is not a real one: the
  tag can show up on googlesource a little after chromiumdash lists it, and
  googlesource throttles (429)."""
  for attempt in range(CHECK_ATTEMPTS):
    result = run_check(version)
    if check_complete(result) or attempt == CHECK_ATTEMPTS - 1:
      return result
    print('%s: no complete answer; trying again' % version, file=sys.stderr)
    time.sleep(60 * 2**attempt)


def git_output(*args):
  return subprocess.run(['git', *args], cwd=ROOT, capture_output=True,
                        text=True, check=True).stdout.strip()


def gh(*args, dry_run=False, stdin=None):
  if dry_run:
    print('[dry run] gh', ' '.join(args))
    return ''
  try:
    return subprocess.run(['gh', *args], cwd=ROOT, input=stdin,
                          capture_output=True, text=True, check=True).stdout
  except subprocess.CalledProcessError as e:
    print('gh %s failed: %s' % (args[0], e.stderr), file=sys.stderr)
    raise


def find_issue(dry_run):
  if dry_run:
    return None
  out = gh('issue', 'list', '--label', LABEL, '--state', 'open', '--json',
           'number,body', '--limit', '1')
  issues = json.loads(out)
  return issues[0] if issues else None


def main(argv=None):
  parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
  parser.add_argument('--dry-run', action='store_true')
  parser.add_argument('--pin', help=argparse.SUPPRESS)
  args = parser.parse_args(argv)

  pin = args.pin or (ROOT / 'CHROMIUM_VERSION').read_text().splitlines()[0]
  releases = fetch_releases()
  # chromiumdash answering with nothing of our milestone is a fault on its
  # side (or a pin that fell out of both channels): say so, and leave the
  # issue alone rather than close it.
  if not any(r.milestone == milestone(pin) for r in releases):
    sys.exit('chromiumdash lists no M%d release in Beta or Stable; not '
             'touching the tracking issue' % milestone(pin))
  targets = pick_targets(pin, releases)
  issue = find_issue(args.dry_run)
  print('pin %s; point release: %s; new milestone: %s' %
        (pin, targets['point'].version if 'point' in targets else '-',
         targets['milestone'].version if 'milestone' in targets else '-'))

  if not targets:
    if issue:
      gh('issue', 'close', str(issue['number']), '--comment',
         'Koan is on %s, the newest of its milestone, and no newer milestone '
         'is at Stable.' % pin, dry_run=args.dry_run)
    return 0

  state = state_of(pin, targets, git_output('rev-parse', 'HEAD:patches'))
  old = marker_state(issue['body']) if issue else None
  if same_issue(old, state):
    print('Same as the last run; nothing to do.')
    return 0

  checks = {r.version: check_with_retries(r.version) for r in targets.values()}
  state = marker_of(state, checks)
  server = os.environ.get('GITHUB_SERVER_URL', 'https://github.com')
  repo = os.environ.get('GITHUB_REPOSITORY', '')
  run_id = os.environ.get('GITHUB_RUN_ID', '')
  run_url = '%s/%s/actions/runs/%s' % (server, repo, run_id) if run_id else ''
  body = render_body(pin, targets, checks, state, run_url)
  title = title_for(targets)
  summary = os.environ.get('GITHUB_STEP_SUMMARY')
  if summary:
    with open(summary, 'a') as f:
      f.write('## %s\n\n%s\n' % (title, body))
  if args.dry_run:
    print(title)
    print(body)
    return 0

  if issue is None:
    gh('label', 'create', LABEL, '--color', '4285F4', '--description',
       'Chromium has a newer release to take', '--force')
    gh('issue', 'create', '--title', title, '--label', LABEL, '--body-file',
       '-', stdin=body)
    return 0
  gh('issue', 'edit', str(issue['number']), '--title', title, '--body-file',
     '-', stdin=body)
  # Editing doesn't notify: say so when there's a new version to take.
  if old is None or (old.get('point'), old.get('milestone')) != (
      state['point'], state['milestone']):
    gh('issue', 'comment', str(issue['number']), '--body',
       'New: ' + title.split(': ', 1)[1])
  return 0


if __name__ == '__main__':
  sys.exit(main())
