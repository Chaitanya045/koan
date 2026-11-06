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
    rules = [('A.x', 'Allow', 'None'), ('A.y', 'Deny', 'None')]
    self.assertEqual(cov.check(self.PROTOCOL, rules, ['B', 'C']), [])

  def test_a_new_method_in_a_listed_domain_is_reported(self):
    problems = cov.check(self.PROTOCOL, [('A.x', 'Allow', 'None')], ['B', 'C'])
    self.assertEqual(problems, ['A.y is in the protocol but has no rule'])

  def test_a_new_domain_is_reported(self):
    rules = [('A.x', 'Allow', 'None'), ('A.y', 'Deny', 'None')]
    problems = cov.check(self.PROTOCOL, rules, ['B'])
    self.assertEqual(len(problems), 1)
    self.assertIn('domain C is not classified', problems[0])

  def test_stale_rules_and_domains_are_reported(self):
    rules = [('A.gone', 'Deny', 'None'), ('A.x', 'Allow', 'None'),
             ('A.y', 'Deny', 'None')]
    problems = cov.check(self.PROTOCOL, rules, ['B', 'C', 'Gone'])
    self.assertEqual(len(problems), 2)

  def test_a_domain_cannot_be_listed_and_denied(self):
    rules = [('A.x', 'Allow', 'None'), ('A.y', 'Deny', 'None')]
    problems = cov.check(self.PROTOCOL, rules, ['A', 'B', 'C'])
    self.assertEqual(len(problems), 1)
    self.assertIn('both', problems[0])

  def test_unsorted_tables_are_reported(self):
    rules = [('A.y', 'Deny', 'None'), ('A.x', 'Allow', 'None')]
    problems = cov.check(self.PROTOCOL, rules, ['C', 'B'])
    self.assertEqual(len(problems), 2)


class PolicyTableTest(unittest.TestCase):

  @classmethod
  def setUpClass(cls):
    rules, denied = cov.parse_policy(cov.POLICY.read_text())
    cls.rules = {m: (v, g) for m, v, g in rules}
    cls.ordered = [m for m, _, _ in rules]
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

  def test_what_an_agent_needs_is_allowed(self):
    for method in ('Page.navigate', 'Page.captureScreenshot', 'Runtime.evaluate',
                   'Runtime.callFunctionOn', 'DOM.getDocument',
                   'Accessibility.getFullAXTree', 'Input.dispatchMouseEvent',
                   'Input.dispatchKeyEvent', 'Network.enable', 'IO.read'):
      self.assertEqual(self.verdict(method), 'Allow', method)


if __name__ == '__main__':
  unittest.main()
