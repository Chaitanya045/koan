#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for the agent containers' per-profile service guard.

scripts/agent/check_agent_services.py checks the C++ table
(src/browser/containers/koan_agent_service_policy.cc) against the inventory of
the pinned Chromium; these pin what it must catch, and that the real table
passes against the real inventory.
"""

import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'agent'))
import check_agent_services as svc  # noqa: E402

COMMIT = 'a' * 40


def policy(*rows):
  return '\n'.join(['x', svc.BEGIN] + list(rows) + [svc.END, 'y'])


def inventory(*names, commit=COMMIT):
  return {'chromium_commit': commit,
          'factories': {n: 'chrome/browser/%s.cc' % n for n in names}}


class ParseTest(unittest.TestCase):

  def test_wrapped_rows_and_split_reasons(self):
    rules, problems = svc.parse_policy(policy(
        '    {"Alpha", ServiceVerdict::kSafe, "no tab data"},',
        '    {"Beta", ServiceVerdict::kBlocked,',
        '     "B4: passwords "',
        '     "are never filled"},'))
    self.assertEqual(problems, [])
    self.assertEqual(rules, [('Alpha', 'Safe', 'no tab data'),
                             ('Beta', 'Blocked', 'B4: passwords are never filled')])

  def test_a_row_that_does_not_parse_is_an_error(self):
    _, problems = svc.parse_policy(policy(
        '    {"Alpha", ServiceVerdict::kSafe, "ok"},',
        '    {"Beta", ServiceVerdict::kMaybe, "typo"},'))
    self.assertEqual(len(problems), 1)

  def test_missing_markers(self):
    _, problems = svc.parse_policy('nothing here')
    self.assertEqual(len(problems), 1)


class CheckTest(unittest.TestCase):

  def test_a_new_service_fails(self):
    rules = [('Alpha', 'Safe', 'x')]
    problems, _ = svc.check(rules, inventory('Alpha', 'Brand'), COMMIT)
    self.assertEqual(len(problems), 1)
    self.assertIn('Brand', problems[0])

  def test_an_old_inventory_fails(self):
    rules = [('Alpha', 'Safe', 'x')]
    problems, _ = svc.check(rules, inventory('Alpha', commit='b' * 40), COMMIT)
    self.assertEqual(len(problems), 1)

  def test_unsorted_or_duplicate_rows_fail(self):
    for rules in ([('B', 'Safe', 'x'), ('A', 'Safe', 'x')],
                  [('A', 'Safe', 'x'), ('A', 'Gap', 'x')]):
      problems, _ = svc.check(rules, inventory('A', 'B'), COMMIT)
      self.assertTrue(problems)

  def test_extra_rows_and_gaps_are_notes(self):
    rules = [('A', 'Gap', 'why'), ('Z', 'Safe', 'runtime only')]
    problems, notes = svc.check(rules, inventory('A'), COMMIT)
    self.assertEqual(problems, [])
    self.assertEqual(len(notes), 2)


class RealTableTest(unittest.TestCase):

  def test_the_policy_decides_the_pinned_inventory(self):
    rules, problems = svc.parse_policy(svc.POLICY.read_text())
    self.assertEqual(problems, [])
    pinned = svc.VERSION.read_text().split()[1]
    more, _ = svc.check(rules, json.loads(svc.INVENTORY.read_text()), pinned)
    self.assertEqual(more, [])


if __name__ == '__main__':
  unittest.main()
