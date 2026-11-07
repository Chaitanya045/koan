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


EVENTS_CC = """
  // BEGIN EVENTS
  {"Network.requestWillBeSent", EventAction::kScrubNetwork},
  {"Page.frameNavigated", EventAction::kPass},
  // END EVENTS
"""

EVENT_DETAILS = {
    'Network': {'events': {'requestWillBeSent': 'h'}, 'commands': {}},
    'Page': {'events': {'frameNavigated': 'h'}, 'commands': {}},
}


class EventTableTest(unittest.TestCase):

  def test_parses_and_accepts_a_good_table(self):
    events = cov.parse_events(EVENTS_CC)
    self.assertEqual(events, [('Network.requestWillBeSent', 'ScrubNetwork'),
                              ('Page.frameNavigated', 'Pass')])
    self.assertEqual(cov.check_events(EVENT_DETAILS, events, []), [])

  def test_a_malformed_row_is_an_error(self):
    with self.assertRaises(ValueError):
      cov.parse_events(EVENTS_CC.replace('EventAction::kPass}', 'Pass}'))

  def test_a_network_event_must_be_scrubbed_and_only_those(self):
    events = [('Network.requestWillBeSent', 'Pass'),
              ('Page.frameNavigated', 'ScrubNetwork')]
    problems = cov.check_events(EVENT_DETAILS, events, [])
    self.assertEqual(len(problems), 2)

  def test_unknown_unsorted_or_denied_events_are_reported(self):
    events = [('Page.frameNavigated', 'Pass'), ('Page.nope', 'Pass'),
              ('Network.requestWillBeSent', 'ScrubNetwork')]
    problems = cov.check_events(EVENT_DETAILS, events, ['Page'])
    self.assertTrue(any('not sorted' in p for p in problems))
    self.assertTrue(any('not in the protocol' in p or 'denied domain' in p
                        for p in problems))


SCHEMA_PDL = """\
domain Net
  type Headers extends object

  type Thing extends object
    properties
      string url
      # comment
      Headers headers
      optional array of Inner inners

  type Inner extends object
    properties
      string label

  event seen
    parameters
      Thing thing
      string id

domain Runtime
  type ExecutionContextId extends integer

  event noisy
    parameters
      ExecutionContextId executionContextId
      string text

  event quiet
    parameters
      string text
"""


class SchemaTest(unittest.TestCase):

  def setUp(self):
    self.schema = pdl.parse_schema_text(SCHEMA_PDL)

  def test_blocks_and_fields_are_found_with_qualified_references(self):
    seen = self.schema['Net.seen']
    self.assertEqual(seen['kind'], 'event')
    self.assertEqual(seen['fields'], [('thing', 'Net.Thing'), ('id', None)])
    self.assertEqual(self.schema['Net.Thing']['fields'],
                     [('url', None), ('headers', 'Net.Headers'),
                      ('inners', 'Net.Inner')])

  def test_the_closure_follows_references(self):
    self.assertEqual(pdl.closure(self.schema, 'Net.seen'),
                     ['Net.Headers', 'Net.Inner', 'Net.Thing', 'Net.seen'])
    names = {f for _, f, _ in pdl.closure_fields(self.schema, 'Net.seen')}
    self.assertEqual(names, {'thing', 'id', 'url', 'headers', 'inners', 'label'})

  def test_dropped_fields_are_not_followed(self):
    fields = pdl.closure_fields(self.schema, 'Net.seen',
                                drop=lambda n: n == 'inners')
    self.assertNotIn('label', {f for _, f, _ in fields})
    self.assertNotIn('inners', {f for _, f, _ in fields})

  def test_a_field_added_three_types_down_changes_the_event_hash(self):
    before = pdl.closure_hash(self.schema, 'Net.seen')
    deeper = pdl.parse_schema_text(SCHEMA_PDL.replace(
        '      string label\n', '      string label\n      string authToken\n'))
    self.assertNotEqual(before, pdl.closure_hash(deeper, 'Net.seen'))
    # but rewording a comment does not
    reworded = pdl.parse_schema_text(SCHEMA_PDL.replace('# comment', '# other'))
    self.assertEqual(before, pdl.closure_hash(reworded, 'Net.seen'))

  def test_the_header_line_of_a_type_is_part_of_it(self):
    # `type X extends string` retargeted to `extends array of Cookie` changes
    # what X is, though no field line changed.
    before = pdl.closure_hash(self.schema, 'Net.seen')
    retargeted = pdl.parse_schema_text(
        SCHEMA_PDL.replace('type Inner extends object',
                           'type Inner extends array of Net.Headers'))
    self.assertNotEqual(before, pdl.closure_hash(retargeted, 'Net.seen'))

  def test_extends_is_followed_into_the_closure(self):
    text = SCHEMA_PDL + """
domain Net2
  type Wrapper extends array of Net.Headers

  event wrapped
    parameters
      Wrapper w
"""
    schema = pdl.parse_schema_text(text)
    self.assertEqual(schema['Net2.Wrapper']['fields'],
                     [('<extends>', 'Net.Headers')])
    self.assertIn('Net.Headers', pdl.closure(schema, 'Net2.wrapped'))
    # a builtin base adds nothing
    self.assertEqual(self.schema['Net.Headers']['fields'], [])
    # and a Pass event that reaches a header map through extends is a leak
    leaks = cov.leaks_in_pass_event(schema, 'Net2.wrapped')
    self.assertTrue(any('<extends>' in leak for leak in leaks), leaks)

  def test_execution_context_types_are_secret_for_unscrubbed_events(self):
    self.assertIn('ExecutionContextId', cov.SECRET_TYPES)
    self.assertIn('ExecutionContextUniqueId', cov.SECRET_TYPES)

  def test_the_drift_is_reported_by_the_baseline_check(self):
    events = [('Net.seen', 'Pass')]
    rules = [('Net.enable', 'Allow', 'None', 'None')]
    details = pdl.parse_details_text(SCHEMA_PDL)
    old = cov.snapshot(details, rules, self.schema, events)
    deeper = pdl.parse_schema_text(SCHEMA_PDL.replace(
        '      string label\n', '      string label\n      string authToken\n'))
    new = cov.snapshot(details, rules, deeper, events)
    problems = cov.check_baseline(new, old)
    self.assertTrue(any('schema of event Net.seen' in p for p in problems))

  def test_a_scrubbed_event_listing_shows_a_new_field_that_survives(self):
    events = [('Net.seen', 'ScrubNetwork')]
    details = pdl.parse_details_text(SCHEMA_PDL)
    old = cov.snapshot(details, [], self.schema, events)
    deeper = pdl.parse_schema_text(SCHEMA_PDL.replace(
        '      string label\n', '      string label\n      string authToken\n'))
    new = cov.snapshot(details, [], deeper, events)
    problems = cov.check_baseline(new, old)
    self.assertTrue(any('Net.Inner.authToken' in p for p in problems))
    # a new cookie-named field is removed by the scrubber and not listed
    cookie = pdl.parse_schema_text(SCHEMA_PDL.replace(
        '      string label\n', '      string label\n      string sessionCookie\n'))
    kept = cov.snapshot(details, [], cookie, events)['network_event_fields']
    self.assertNotIn('Net.Inner.sessionCookie', kept)

  def test_an_unscrubbed_event_may_not_reach_a_header_map_or_a_context(self):
    details = {'Net': {'events': {'seen': 'h'}, 'commands': {}},
               'Runtime': {'events': {'noisy': 'h', 'quiet': 'h'},
                           'commands': {}}}
    ok = cov.check_events(details, [('Runtime.quiet', 'Pass')], [], self.schema)
    self.assertEqual(ok, [])
    leaks = cov.check_events(details, [('Net.seen', 'Pass')], [], self.schema)
    self.assertTrue(any('Net.Thing.headers' in p for p in leaks), leaks)
    ctx = cov.check_events(details, [('Runtime.noisy', 'Pass')], [], self.schema)
    self.assertTrue(any('executionContextId' in p for p in ctx), ctx)
    held = cov.check_events(details, [('Runtime.noisy', 'HoldForContextTracker')],
                            [], self.schema)
    self.assertEqual(held, [])


class RealEventTableTest(unittest.TestCase):

  @classmethod
  def setUpClass(cls):
    cls.events = cov.parse_events(cov.EVENTS.read_text())

  def test_no_event_delivered_as_it_is_reaches_a_secret_or_a_context(self):
    import pdl as _pdl
    schema = _pdl.load_schema(cov.ROOT / 'chromium/src') if (
        cov.ROOT / 'chromium/src').exists() else None
    if schema is None:
      self.skipTest('no Chromium checkout (scripts/ci/check_agent_policy.sh '
                    'runs this check against the pinned protocol)')
    for name, action in self.events:
      if action == 'Pass':
        self.assertEqual(cov.leaks_in_pass_event(schema, name), [], name)

  def test_it_is_sorted_and_every_network_event_is_scrubbed(self):
    names = [e for e, _ in self.events]
    self.assertEqual(names, sorted(names))
    self.assertGreater(len(names), 30)
    for name, action in self.events:
      self.assertEqual(name.startswith('Network.'), action == 'ScrubNetwork', name)

  def test_no_event_of_a_denied_domain_is_allowed(self):
    _, denied = cov.parse_policy(cov.POLICY.read_text())
    for name, _ in self.events:
      self.assertNotIn(name.split('.')[0], denied, name)

  def test_the_events_that_would_leak_are_not_in_the_list(self):
    names = {e for e, _ in self.events}
    for leaky in ('Network.dataReceived', 'Network.responseReceivedEarlyHints',
                  'Network.webSocketWillSendHandshakeRequest',
                  'Network.webSocketHandshakeResponseReceived',
                  'Network.eventSourceMessageReceived',
                  'Network.requestIntercepted', 'Fetch.requestPaused',
                  'Storage.cookieListItemAdded', 'Page.fileChooserOpened',
                  'Page.downloadWillBegin'):
      self.assertNotIn(leaky, names)

  def test_execution_contexts_wait_for_the_tracker(self):
    actions = dict(self.events)
    for name in ('Runtime.executionContextCreated',
                 'Runtime.executionContextDestroyed',
                 'Runtime.executionContextsCleared'):
      self.assertEqual(actions[name], 'HoldForContextTracker')

  def test_console_exceptions_and_bindings_wait_too(self):
    # They carry executionContextId: an extension's content script would
    # otherwise reach the agent through its console output.
    actions = dict(self.events)
    for name in ('Runtime.consoleAPICalled', 'Runtime.exceptionThrown',
                 'Runtime.bindingCalled'):
      self.assertEqual(actions[name], 'HoldForContextTracker', name)


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
        'DOM.resolveNode': 'RuntimeContext',
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
