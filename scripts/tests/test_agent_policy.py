#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for agent control's method policy table and its coverage check.

The C++ tests (src/browser/agent/koan_agent_policy_unittest.cc) run on the Mac.
These run in CI and anywhere: they parse the C++ table itself and pin the
rules that must never change by accident.
"""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / 'agent'))
import check_policy_coverage as cov  # noqa: E402
import pdl  # noqa: E402

PDL = """\
# comment
version
  major 1
  minor 3

domain Alpha
  depends on Beta

  type Thing extends object
    properties
      string command

  command first
    parameters
      string x

  experimental command second

  deprecated command third

  experimental deprecated command fourth

  event happened

experimental domain Beta
  command only
"""


class PdlTest(unittest.TestCase):

  def test_finds_commands_with_modifiers_and_ignores_the_rest(self):
    self.assertEqual(
        pdl.parse_pdl_text(PDL), {
            'Alpha': ['first', 'second', 'third', 'fourth'],
            'Beta': ['only'],
        })


class CheckTest(unittest.TestCase):

  PROTOCOL = {'A': ['x', 'y'], 'B': ['z'], 'C': ['w']}

  def test_a_consistent_policy_has_no_problems(self):
    rules = [('A.x', 'Allow', 'None', 'None'), ('A.y', 'Deny', 'None', 'None')]
    self.assertEqual(cov.check(self.PROTOCOL, rules, ['B', 'C']), [])

  def test_a_new_method_in_a_listed_domain_is_reported(self):
    problems = cov.check(self.PROTOCOL, [('A.x', 'Allow', 'None', 'None')], ['B', 'C'])
    self.assertEqual(problems, ['A.y is in the protocol but has no rule'])

  def test_a_new_domain_is_reported(self):
    rules = [('A.x', 'Allow', 'None', 'None'), ('A.y', 'Deny', 'None', 'None')]
    problems = cov.check(self.PROTOCOL, rules, ['B'])
    self.assertEqual(len(problems), 1)
    self.assertIn('domain C is not classified', problems[0])

  def test_stale_rules_and_domains_are_reported(self):
    rules = [('A.gone', 'Deny', 'None', 'None'), ('A.x', 'Allow', 'None', 'None'),
             ('A.y', 'Deny', 'None', 'None')]
    problems = cov.check(self.PROTOCOL, rules, ['B', 'C', 'Gone'])
    self.assertEqual(len(problems), 2)

  def test_a_domain_cannot_be_listed_and_denied(self):
    rules = [('A.x', 'Allow', 'None', 'None'), ('A.y', 'Deny', 'None', 'None')]
    problems = cov.check(self.PROTOCOL, rules, ['A', 'B', 'C'])
    self.assertEqual(len(problems), 1)
    self.assertIn('both', problems[0])

  def test_unsorted_tables_are_reported(self):
    rules = [('A.y', 'Deny', 'None', 'None'), ('A.x', 'Allow', 'None', 'None')]
    problems = cov.check(self.PROTOCOL, rules, ['C', 'B'])
    self.assertEqual(len(problems), 2)


DETAILS_PDL = """\
domain Alpha
  command first
    # a comment that may be reworded
    parameters
      string x
    returns
      boolean ok

  command second

  event happened
    parameters
      string why
"""

DETAILS_PDL_REWORDED = DETAILS_PDL.replace('a comment that may be reworded',
                                          'a different comment')
DETAILS_PDL_NEW_PARAM = DETAILS_PDL.replace('      string x\n',
                                           '      string x\n      optional boolean grant\n')


class DetailsTest(unittest.TestCase):

  def test_hashes_ignore_comments_and_change_with_parameters(self):
    base = pdl.parse_details_text(DETAILS_PDL)
    self.assertEqual(sorted(base['Alpha']['commands']), ['first', 'second'])
    self.assertEqual(list(base['Alpha']['events']), ['happened'])
    reworded = pdl.parse_details_text(DETAILS_PDL_REWORDED)
    self.assertEqual(base, reworded)
    changed = pdl.parse_details_text(DETAILS_PDL_NEW_PARAM)
    self.assertNotEqual(base['Alpha']['commands']['first'],
                        changed['Alpha']['commands']['first'])
    self.assertEqual(base['Alpha']['commands']['second'],
                     changed['Alpha']['commands']['second'])


class BaselineTest(unittest.TestCase):

  RULES = [('Alpha.first', 'Allow', 'None', 'None'),
           ('Alpha.second', 'Deny', 'None', 'None')]

  def snap(self, text):
    return cov.snapshot(pdl.parse_details_text(text), self.RULES)

  def test_only_forwarded_methods_are_recorded(self):
    snap = self.snap(DETAILS_PDL)
    self.assertEqual(list(snap['params']), ['Alpha.first'])
    self.assertEqual(snap['events'], {'Alpha': ['happened']})

  def test_an_unchanged_roll_has_no_problems(self):
    self.assertEqual(
        cov.check_baseline(self.snap(DETAILS_PDL_REWORDED),
                           self.snap(DETAILS_PDL)), [])

  def test_a_new_parameter_on_a_forwarded_method_is_reported(self):
    problems = cov.check_baseline(self.snap(DETAILS_PDL_NEW_PARAM),
                                  self.snap(DETAILS_PDL))
    self.assertEqual(len(problems), 1)
    self.assertIn('Alpha.first changed', problems[0])

  def test_a_new_event_is_reported(self):
    new = DETAILS_PDL + "\n  event surprise\n"
    problems = cov.check_baseline(self.snap(new), self.snap(DETAILS_PDL))
    self.assertEqual(len(problems), 1)
    self.assertIn('new event Alpha.surprise', problems[0])

  def test_a_newly_forwarded_method_must_be_in_the_baseline(self):
    rules = self.RULES + [('Alpha.third', 'Allow', 'None', 'None')]
    text = DETAILS_PDL + "\n  command third\n"
    current = cov.snapshot(pdl.parse_details_text(text), rules)
    problems = cov.check_baseline(current, self.snap(DETAILS_PDL))
    self.assertTrue(any('Alpha.third is forwarded but is not in the baseline' in p
                        for p in problems))


class ParseTest(unittest.TestCase):

  GOOD = """
  // BEGIN RULES
  {"A.x", Verdict::kAllow, Capability::kNone, ParamFilter::kNone},
  {"A.y", Verdict::kDeny,
   Capability::kNone, ParamFilter::kNone},
  // END RULES
  // BEGIN DENIED DOMAINS
  "B",
  // END DENIED DOMAINS
  """

  def test_parses_rows_wrapped_over_two_lines(self):
    rules, denied = cov.parse_policy(self.GOOD)
    self.assertEqual([r[0] for r in rules], ['A.x', 'A.y'])
    self.assertEqual(denied, ['B'])

  def test_a_malformed_row_is_an_error_not_a_silent_skip(self):
    bad = self.GOOD.replace('Capability::kNone, ParamFilter::kNone},\n  //',
                            'Capability::kNone},\n  //')
    self.assertNotEqual(bad, self.GOOD)
    with self.assertRaises(ValueError):
      cov.parse_policy(bad)


class PolicyTableTest(unittest.TestCase):

  @classmethod
  def setUpClass(cls):
    rules, denied = cov.parse_policy(cov.POLICY.read_text())
    cls.rules = {m: (v, g) for m, v, g, _ in rules}
    cls.filters = {m: f for m, _, _, f in rules}
    cls.ordered = [m for m, _, _, _ in rules]
    cls.denied = denied

  def verdict(self, method):
    return self.rules.get(method, ('Deny', 'None'))[0]

  def test_the_table_is_not_empty_and_sorted(self):
    self.assertGreater(len(self.rules), 100)
    self.assertEqual(self.ordered, sorted(self.ordered))
    self.assertEqual(len(set(self.ordered)), len(self.ordered))
    self.assertEqual(self.denied, sorted(self.denied))

  def test_nothing_with_cookies_or_storage_is_ever_reachable(self):
    for method, (verdict, _) in self.rules.items():
      if verdict != 'Deny':
        for word in ('Cookie', 'Storage', 'loadNetworkResource',
                     'clearBrowserCache'):
          self.assertNotIn(word, method)
    for domain in ('Storage', 'DOMStorage', 'IndexedDB', 'CacheStorage',
                   'Browser', 'SystemInfo', 'Extensions', 'Security',
                   'Autofill', 'Tracing', 'Debugger'):
      self.assertNotIn(
          domain,
          {m.split('.')[0] for m, (v, _) in self.rules.items() if v == 'Allow'})

  def test_the_browser_domain_offers_only_the_version_to_the_facade(self):
    browser = {m: v for m, (v, _) in self.rules.items() if m.startswith('Browser.')}
    self.assertEqual({m for m, v in browser.items() if v != 'Deny'},
                     {'Browser.getVersion'})
    self.assertEqual(browser['Browser.getVersion'], 'Facade')

  def test_agent_tabs_are_never_raised_or_given_new_contexts(self):
    self.assertEqual(self.verdict('Page.bringToFront'), 'NoOp')
    self.assertEqual(self.verdict('Target.activateTarget'), 'NoOp')
    for method in ('Target.createBrowserContext', 'Target.disposeBrowserContext',
                   'Target.attachToBrowserTarget', 'Target.exposeDevToolsProtocol',
                   'Target.setRemoteLocations'):
      self.assertEqual(self.verdict(method), 'Deny', method)

  def test_input_that_would_lock_out_or_drag_files_is_denied(self):
    for method in ('Input.setIgnoreInputEvents', 'Input.dispatchDragEvent',
                   'Input.setInterceptDrags', 'Input.cancelDragging'):
      self.assertEqual(self.verdict(method), 'Deny', method)

  def test_local_file_and_download_methods_are_denied(self):
    for method in ('DOM.setFileInputFiles', 'DOM.getFileInfo',
                   'Page.setDownloadBehavior', 'Page.setInterceptFileChooserDialog',
                   'Page.setBypassCSP', 'Page.setWebLifecycleState',
                   'Emulation.setFocusEmulationEnabled',
                   'Emulation.setVirtualTimePolicy', 'IO.resolveBlob'):
      self.assertEqual(self.verdict(method), 'Deny', method)

  def test_network_interception_and_bodies_are_gated_by_the_capability(self):
    for method in ('Fetch.enable', 'Fetch.continueRequest',
                   'Network.setExtraHTTPHeaders', 'Network.getResponseBody',
                   'Network.getRequestPostData'):
      self.assertEqual(self.rules[method], ('Gated', 'Network'), method)
    gated = {m for m, (v, _) in self.rules.items() if v == 'Gated'}
    self.assertTrue(all(g == 'Network' for m, (v, g) in self.rules.items()
                        if v == 'Gated'))
    self.assertTrue(all(g == 'None' for m, (v, g) in self.rules.items()
                        if v != 'Gated'))
    self.assertIn('Fetch.enable', gated)

  def test_rows_that_need_a_parameter_filter_carry_it(self):
    expected = {
        'Page.navigate': 'NavigationUrl',
        'Target.createTarget': 'NavigationUrl',
        'Input.dispatchKeyEvent': 'StripKeyCommands',
        'Page.createIsolatedWorld': 'IsolatedWorld',
        'Runtime.evaluate': 'RuntimeContext',
        'Runtime.callFunctionOn': 'RuntimeContext',
        'Runtime.addBinding': 'RuntimeContext',
        'Page.captureScreenshot': 'Screenshot',
        'Network.enable': 'NetworkEnable',
        'IO.read': 'OwnStream',
        'IO.close': 'OwnStream',
    }
    for command in ('clearDeviceMetricsOverride', 'clearGeolocationOverride',
                    'setDefaultBackgroundColorOverride', 'setDeviceMetricsOverride',
                    'setEmulatedMedia', 'setGeolocationOverride',
                    'setLocaleOverride', 'setScrollbarsHidden',
                    'setTimezoneOverride', 'setTouchEmulationEnabled',
                    'setUserAgentOverride'):
      expected['Emulation.' + command] = 'OwnTabOnly'
    actual = {m: f for m, f in self.filters.items() if f != 'None'}
    self.assertEqual(actual, expected)
    for method in actual:
      self.assertIn(self.rules[method][0], ('Allow', 'Facade'), method)

  def test_the_isolated_world_filter_is_on_the_row(self):
    # grantUniveralAccess (upstream's spelling) lets an isolated world bypass
    # the same-origin policy; the filter must force it false.
    self.assertEqual(self.filters['Page.createIsolatedWorld'], 'IsolatedWorld')

  def test_what_an_agent_needs_is_allowed(self):
    for method in ('Page.navigate', 'Page.captureScreenshot', 'Runtime.evaluate',
                   'Runtime.callFunctionOn', 'DOM.getDocument',
                   'Accessibility.getFullAXTree', 'Input.dispatchMouseEvent',
                   'Input.dispatchKeyEvent', 'Network.enable', 'IO.read'):
      self.assertEqual(self.verdict(method), 'Allow', method)


if __name__ == '__main__':
  unittest.main()
