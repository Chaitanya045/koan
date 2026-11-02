#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/ci/chromium_releases.py (what to take, the issue text)."""

import pathlib
import sys
import unittest
from unittest import mock

sys.path.insert(0,
                str(pathlib.Path(__file__).resolve().parent.parent / 'ci'))
import chromium_releases as cr  # noqa: E402

MS = 1_793_000_000_000  # An arbitrary release time, in ms.


def rel(version, channel='Stable', ms=MS):
  return cr.Release(version, channel, ms)


class VersionTest(unittest.TestCase):

  def test_compares_numerically_not_as_text(self):
    self.assertGreater(
        cr.version_key('158.0.8127.10'), cr.version_key('158.0.8127.9'))
    self.assertGreater(
        cr.version_key('158.0.8200.1'), cr.version_key('158.0.8127.9'))

  def test_rejects_things_that_are_not_versions(self):
    with self.assertRaises(ValueError):
      cr.Release('158.0.8127', 'Beta', MS)
    with self.assertRaises(ValueError):
      cr.Release('158.0.8127.2; rm -rf /', 'Beta', MS)


class PickTargetsTest(unittest.TestCase):

  def test_nothing_newer(self):
    releases = [rel('156.0.8078.80'), rel('158.0.8127.2', 'Beta')]
    self.assertEqual(cr.pick_targets('158.0.8127.2', releases), {})

  def test_beta_point_release_of_our_milestone(self):
    releases = [
        rel('156.0.8078.80'),
        rel('158.0.8127.2', 'Beta'),
        rel('158.0.8127.9', 'Beta'),
        rel('159.0.8130.0', 'Dev'),
    ]
    targets = cr.pick_targets('158.0.8127.2', releases)
    self.assertEqual(targets['point'].version, '158.0.8127.9')
    self.assertNotIn('milestone', targets)

  def test_takes_the_newest_point_release_and_ignores_older_ones(self):
    releases = [rel('158.0.8127.9', 'Beta'), rel('158.0.8127.10', 'Beta'),
                rel('158.0.8100.4')]
    targets = cr.pick_targets('158.0.8127.2', releases)
    self.assertEqual(targets['point'].version, '158.0.8127.10')

  def test_the_same_version_in_both_channels_counts_as_stable(self):
    releases = [rel('158.0.8127.9', 'Beta'), rel('158.0.8127.9', 'Stable')]
    targets = cr.pick_targets('158.0.8127.2', releases)
    self.assertEqual(targets['point'].channel, 'Stable')

  def test_a_newer_milestone_counts_only_at_stable(self):
    releases = [rel('159.0.8200.1', 'Beta'), rel('160.0.8300.0', 'Beta')]
    self.assertEqual(cr.pick_targets('158.0.8127.2', releases), {})
    releases.append(rel('159.0.8200.1', 'Stable'))
    targets = cr.pick_targets('158.0.8127.2', releases)
    self.assertEqual(targets['milestone'].version, '159.0.8200.1')

  def test_both_at_once(self):
    releases = [rel('158.0.8127.9'), rel('159.0.8200.1'), rel('159.0.8200.4')]
    targets = cr.pick_targets('158.0.8127.2', releases)
    self.assertEqual(targets['point'].version, '158.0.8127.9')
    self.assertEqual(targets['milestone'].version, '159.0.8200.4')


class CheckOutputTest(unittest.TestCase):
  OUTPUT = '\n'.join([
      'Checking patches against Chromium 158.0.8127.2 (681cd621)',
      'ok            a/b.cc',
      'CONFLICT      chrome/browser/x.cc',
      'error: patch failed: chrome/browser/x.cc:14',
      'MISSING       chrome/browser/y.cc',
      'ok            c.gn',
  ])

  def test_parse_counts_patches_and_lists_failures(self):
    total, bad = cr.parse_check(self.OUTPUT)
    self.assertEqual(total, 4)
    self.assertEqual(bad, [('CONFLICT', 'chrome/browser/x.cc'),
                           ('MISSING', 'chrome/browser/y.cc')])

  def test_summaries(self):
    self.assertEqual(cr.summarize_check((3, [])), 'all 3 patches apply')
    self.assertIn('2 of 4 patches fail', cr.summarize_check(cr.parse_check(self.OUTPUT)))
    self.assertIn('tag not found', cr.summarize_check(None))
    self.assertIn('no result', cr.summarize_check((0, [])))


class IssueTest(unittest.TestCase):

  def test_body_names_versions_due_dates_and_failures(self):
    targets = {'point': rel('158.0.8127.9', 'Beta')}
    state = cr.state_of('158.0.8127.2', targets, 'abc')
    body = cr.render_body('158.0.8127.2', targets,
                          {'158.0.8127.9': (76, [('CONFLICT', 'a/b.cc')])},
                          state, 'https://example.test/run/1')
    self.assertIn('158.0.8127.2', body)
    self.assertIn('| Point release of M158 | **158.0.8127.9** | Beta |', body)
    self.assertIn('1 of 76 patches fail', body)
    self.assertNotIn('New milestone', body)
    self.assertIn('https://example.test/run/1', body)

  def test_due_dates_follow_the_policy(self):
    release = rel('158.0.8127.9', ms=1_793_000_000_000)
    day = release.released.strftime('%Y-%m-%d')
    self.assertEqual(cr.due(release, 0), day)
    self.assertNotEqual(cr.due(release, cr.NEW_MILESTONE_DAYS), day)

  def test_state_marker_round_trips_and_ignores_other_text(self):
    targets = {'point': rel('158.0.8127.9'), 'milestone': rel('159.0.8200.1')}
    state = cr.state_of('158.0.8127.2', targets, 'abc')
    body = cr.render_body('158.0.8127.2', targets, {}, state)
    self.assertEqual(cr.marker_state(body), state)
    self.assertIsNone(cr.marker_state('no marker here'))
    self.assertIsNone(cr.marker_state(None))
    self.assertIsNone(cr.marker_state('<!-- koan-chromium-update: {not json} -->'))

  def test_title_lists_the_versions(self):
    targets = {'point': rel('158.0.8127.9'), 'milestone': rel('159.0.8200.1')}
    self.assertEqual(cr.title_for(targets),
                     'Chromium update available: 158.0.8127.9, 159.0.8200.1')


class MainTest(unittest.TestCase):
  """main() with chromiumdash, git, the patch check and gh replaced."""

  def run_main(self, releases, issue=None, check=(2, []), pin='158.0.8127.2'):
    calls = []

    def fake_gh(*args, dry_run=False, stdin=None):
      calls.append((args, stdin))
      return ''

    with mock.patch.object(cr, 'fetch_releases', return_value=releases), \
        mock.patch.object(cr, 'find_issue', return_value=issue), \
        mock.patch.object(cr, 'git_output', return_value='tree1'), \
        mock.patch.object(cr, 'run_check', return_value=check), \
        mock.patch.object(cr, 'gh', side_effect=fake_gh):
      cr.main(['--pin', pin])
    return calls

  def test_opens_an_issue_for_a_new_point_release(self):
    calls = self.run_main([rel('158.0.8127.9', 'Beta')])
    verbs = [args[:2] for args, _ in calls]
    self.assertEqual(verbs, [('label', 'create'), ('issue', 'create')])
    create_args, body = calls[1]
    self.assertIn('Chromium update available: 158.0.8127.9', create_args)
    self.assertIn('all 2 patches apply', body)

  def test_does_nothing_when_nothing_changed(self):
    releases = [rel('158.0.8127.9', 'Beta')]
    state = cr.state_of('158.0.8127.2', cr.pick_targets('158.0.8127.2', releases),
                        'tree1')
    issue = {'number': 7, 'body': cr.render_body('158.0.8127.2', {}, {}, state)}
    self.assertEqual(self.run_main(releases, issue), [])

  def test_edits_quietly_when_only_the_patches_changed(self):
    releases = [rel('158.0.8127.9', 'Beta')]
    state = cr.state_of('158.0.8127.2', cr.pick_targets('158.0.8127.2', releases),
                        'older-tree')
    issue = {'number': 7, 'body': cr.render_body('158.0.8127.2', {}, {}, state)}
    verbs = [args[:2] for args, _ in self.run_main(releases, issue)]
    self.assertEqual(verbs, [('issue', 'edit')])

  def test_comments_when_a_newer_version_appears(self):
    old = cr.state_of('158.0.8127.2', {'point': rel('158.0.8127.9')}, 'tree1')
    issue = {'number': 7, 'body': cr.render_body('158.0.8127.2', {}, {}, old)}
    calls = self.run_main([rel('158.0.8127.10', 'Beta')], issue)
    self.assertEqual([args[:2] for args, _ in calls],
                     [('issue', 'edit'), ('issue', 'comment')])

  def test_closes_the_issue_once_the_pin_has_caught_up(self):
    issue = {'number': 7, 'body': 'old'}
    calls = self.run_main([rel('158.0.8127.2', 'Beta')], issue)
    self.assertEqual([args[:2] for args, _ in calls], [('issue', 'close')])

  def test_no_issue_and_nothing_newer_posts_nothing(self):
    self.assertEqual(self.run_main([rel('158.0.8127.2', 'Beta')]), [])


if __name__ == '__main__':
  unittest.main()
