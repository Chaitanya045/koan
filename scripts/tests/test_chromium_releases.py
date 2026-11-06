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


class MarkerTest(unittest.TestCase):

  def test_complete_only_with_real_answers(self):
    ok = (3, [])
    self.assertTrue(cr.check_complete(ok))
    self.assertTrue(cr.check_complete((3, [('CONFLICT', 'a.cc')])))
    self.assertFalse(cr.check_complete(None))
    self.assertFalse(cr.check_complete((0, [])))
    self.assertFalse(cr.check_complete((3, [('FETCH FAILED', 'a.cc')])))

  def test_the_marker_keeps_the_check_results(self):
    state = cr.state_of('158.0.8127.2', {'point': rel('158.0.8127.9')}, 'abc')
    marker = cr.marker_of(state, {'158.0.8127.9': (3, [('CONFLICT', 'a.cc')])})
    self.assertTrue(marker['complete'])
    self.assertEqual(marker['checks']['158.0.8127.9'],
                     {'total': 3, 'bad': [['CONFLICT', 'a.cc']]})
    self.assertTrue(cr.same_issue(marker, state))
    self.assertFalse(cr.same_issue(marker, dict(state, patches='other')))

  def test_an_unfinished_check_is_not_final(self):
    state = cr.state_of('158.0.8127.2', {'point': rel('158.0.8127.9')}, 'abc')
    marker = cr.marker_of(state, {'158.0.8127.9': None})
    self.assertFalse(marker['complete'])
    self.assertFalse(cr.same_issue(marker, state))
    self.assertIn('tries again', cr.render_body(
        '158.0.8127.2', {'point': rel('158.0.8127.9')}, {'158.0.8127.9': None},
        marker))


class MainTest(unittest.TestCase):
  """main() with chromiumdash, git, the patch check and gh replaced."""

  def issue_for(self, releases, checks=None, patches='tree1', pin='158.0.8127.2'):
    targets = cr.pick_targets(pin, releases)
    if checks is None:
      checks = {r.version: (2, []) for r in targets.values()}
    marker = cr.marker_of(cr.state_of(pin, targets, patches), checks)
    return {'number': 7, 'body': cr.render_body(pin, targets, checks, marker)}

  def run_main(self, releases, issue=None, check=(2, []), pin='158.0.8127.2',
               next_issue=None):
    calls = []
    issues = {cr.LABEL: issue, cr.NEXT_LABEL: next_issue}

    def fake_gh(*args, dry_run=False, stdin=None):
      calls.append((args, stdin))
      return ''

    with mock.patch.object(cr, 'fetch_releases', return_value=releases), \
        mock.patch.object(cr, 'find_issue',
                          side_effect=lambda dry, label=cr.LABEL: issues[label]), \
        mock.patch.object(cr, 'git_output', return_value='tree1'), \
        mock.patch.object(cr, 'run_check', return_value=check), \
        mock.patch.object(cr.time, 'sleep'), \
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
    self.assertEqual(self.run_main(releases, self.issue_for(releases)), [])

  def test_edits_quietly_when_only_the_patches_changed(self):
    releases = [rel('158.0.8127.9', 'Beta')]
    issue = self.issue_for(releases, patches='older-tree')
    verbs = [args[:2] for args, _ in self.run_main(releases, issue)]
    self.assertEqual(verbs, [('issue', 'edit')])

  def test_comments_when_a_newer_version_appears(self):
    issue = self.issue_for([rel('158.0.8127.9', 'Beta')])
    calls = self.run_main([rel('158.0.8127.10', 'Beta')], issue)
    self.assertEqual([args[:2] for args, _ in calls],
                     [('issue', 'edit'), ('issue', 'comment')])

  def test_checks_again_when_the_last_check_was_unfinished(self):
    releases = [rel('158.0.8127.9', 'Beta')]
    issue = self.issue_for(releases, checks={'158.0.8127.9': None})
    calls = self.run_main(releases, issue, check=(2, [('CONFLICT', 'a.cc')]))
    self.assertEqual([args[:2] for args, _ in calls], [('issue', 'edit')])
    self.assertIn('1 of 2 patches fail', calls[0][1])

  def test_retries_a_check_that_has_no_answer_yet(self):
    releases = [rel('158.0.8127.9', 'Beta')]
    answers = [None, (0, []), (2, [])]
    with mock.patch.object(cr, 'run_check', side_effect=answers) as run, \
        mock.patch.object(cr.time, 'sleep') as sleep:
      self.assertEqual(cr.check_with_retries('158.0.8127.9'), (2, []))
    self.assertEqual(run.call_count, 3)
    self.assertEqual(sleep.call_count, 2)

  def test_gives_up_retrying_and_reports_what_it_has(self):
    with mock.patch.object(cr, 'run_check', return_value=None) as run, \
        mock.patch.object(cr.time, 'sleep'):
      self.assertIsNone(cr.check_with_retries('158.0.8127.9'))
    self.assertEqual(run.call_count, cr.CHECK_ATTEMPTS)

  def test_closes_the_issue_once_the_pin_has_caught_up(self):
    issue = {'number': 7, 'body': 'old'}
    calls = self.run_main([rel('158.0.8127.2', 'Beta')], issue)
    self.assertEqual([args[:2] for args, _ in calls], [('issue', 'close')])

  def test_no_issue_and_nothing_newer_posts_nothing(self):
    self.assertEqual(self.run_main([rel('158.0.8127.2', 'Beta')]), [])

  def test_an_empty_answer_from_chromiumdash_leaves_the_issue_alone(self):
    issue = {'number': 7, 'body': 'old'}
    with self.assertRaises(SystemExit):
      self.run_main([], issue)
    with self.assertRaises(SystemExit):  # nothing of our milestone
      self.run_main([rel('156.0.8078.80')], issue)


class NextBetaTest(unittest.TestCase):

  def test_picks_the_newest_beta_of_the_first_milestone_that_is_not_stable(self):
    releases = [rel('158.0.8127.18'), rel('159.0.8200.1', 'Beta'),
                rel('159.0.8200.7', 'Beta'), rel('160.0.8300.0', 'Beta')]
    self.assertEqual(
        cr.pick_next_beta('158.0.8127.2', releases).version, '159.0.8200.7')

  def test_none_while_the_beta_is_our_own_or_already_stable(self):
    self.assertIsNone(cr.pick_next_beta(
        '158.0.8127.2', [rel('158.0.8127.18', 'Beta'), rel('157.0.8103.39')]))
    # M159 is at Stable: the pin issue handles it, the next Beta is M160.
    releases = [rel('159.0.8200.7'), rel('159.0.8200.9', 'Beta'),
                rel('160.0.8300.1', 'Beta')]
    self.assertEqual(
        cr.pick_next_beta('158.0.8127.2', releases).version, '160.0.8300.1')
    self.assertIsNone(cr.pick_next_beta('159.0.8200.7', releases[:2]))

  def test_body_is_report_only(self):
    beta = rel('159.0.8200.1', 'Beta')
    marker = cr.next_beta_marker(beta, 'abc', (3, [('CONFLICT', 'a/b.cc')]))
    body = cr.render_next_beta_body('158.0.8127.2', beta, (3, [('CONFLICT', 'a/b.cc')]),
                                    marker)
    self.assertIn('Report only', body)
    self.assertIn('1 of 3 patches fail', body)
    self.assertNotIn('Due', body)
    self.assertEqual(cr.marker_state(body, 'chromium-next-beta'), marker)
    self.assertIsNone(cr.marker_state(body))  # not the pin issue's marker


class NextBetaMainTest(MainTest):

  BETA = [rel('158.0.8127.9'), rel('159.0.8200.1', 'Beta')]

  def next_issue(self, version='159.0.8200.1', check=(2, []), patches='tree1'):
    beta = rel(version, 'Beta')
    marker = cr.next_beta_marker(beta, patches, check)
    return {'number': 9, 'body': cr.render_next_beta_body(
        '158.0.8127.2', beta, check, marker)}

  def verbs(self, calls):
    return [args[:2] for args, _ in calls]

  def test_opens_its_own_issue_with_its_own_label(self):
    calls = self.run_main(self.BETA, issue=self.issue_for(self.BETA[:1]))
    self.assertEqual(self.verbs(calls), [('label', 'create'), ('issue', 'create')])
    self.assertIn(cr.NEXT_LABEL, calls[0][0])
    self.assertIn(cr.NEXT_LABEL, calls[1][0])
    self.assertIn('M159 Beta early warning: 159.0.8200.1', calls[1][0][3])
    self.assertIn('all 2 patches apply', calls[1][1])

  def test_does_nothing_when_nothing_changed(self):
    calls = self.run_main(self.BETA, issue=self.issue_for(self.BETA[:1]),
                          next_issue=self.next_issue())
    self.assertEqual(calls, [])

  def test_a_newer_beta_edits_quietly_unless_a_conflict_appears_or_goes(self):
    old = self.next_issue('159.0.8200.0')
    calls = self.run_main(self.BETA, issue=self.issue_for(self.BETA[:1]),
                          next_issue=old)
    self.assertEqual(self.verbs(calls), [('issue', 'edit')])
    calls = self.run_main(self.BETA, issue=self.issue_for(self.BETA[:1]),
                          next_issue=old, check=(2, [('CONFLICT', 'a.cc')]))
    self.assertEqual(self.verbs(calls), [('issue', 'edit'), ('issue', 'comment')])
    self.assertIn('`a.cc`', calls[1][0][-1])

  def test_unfinished_check_is_repeated(self):
    old = self.next_issue(check=None)
    calls = self.run_main(self.BETA, issue=self.issue_for(self.BETA[:1]),
                          next_issue=old)
    self.assertEqual(self.verbs(calls), [('issue', 'edit')])

  def test_closes_when_the_milestone_reaches_stable_but_not_on_a_gap(self):
    old = self.next_issue()
    stable = [rel('158.0.8127.9'), rel('159.0.8200.5')]
    calls = self.run_main(stable, issue=self.issue_for(stable), next_issue=old)
    self.assertEqual(self.verbs(calls), [('issue', 'close')])
    # M159 neither listed as Beta nor Stable: leave the issue alone.
    calls = self.run_main([rel('158.0.8127.9')],
                          issue=self.issue_for([rel('158.0.8127.9')]),
                          next_issue=old)
    self.assertEqual(calls, [])

  def test_one_issue_failing_does_not_hide_the_other(self):
    with mock.patch.object(cr, 'update_pin_issue', side_effect=RuntimeError('x')), \
        mock.patch.object(cr, 'update_next_beta_issue') as nxt, \
        mock.patch.object(cr, 'fetch_releases', return_value=self.BETA):
      self.assertEqual(cr.main(['--pin', '158.0.8127.2']), 1)
    nxt.assert_called_once()


class FetchTest(unittest.TestCase):

  def test_retries_chromiumdash_then_gives_up(self):
    err = cr.urllib.error.URLError('down')
    with mock.patch.object(cr.urllib.request, 'urlopen', side_effect=err) as op, \
        mock.patch.object(cr.time, 'sleep'):
      with self.assertRaises(cr.urllib.error.URLError):
        cr.fetch_channel('Beta')
    self.assertEqual(op.call_count, cr.FETCH_ATTEMPTS)


if __name__ == '__main__':
  unittest.main()
