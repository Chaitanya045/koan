#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Checks agent control's method policy against a Chromium checkout's protocol.

    scripts/agent/check_policy_coverage.py [--chromium-src DIR] [--list]
    scripts/agent/check_policy_coverage.py [--chromium-src DIR] --update-baseline

The policy (src/browser/agent/koan_agent_policy.cc) is "unlisted = deny" with
per-method tables. This checks the table against the DevTools protocol of the
Chromium checkout (default: $KOAN_CHROMIUM_SRC, else <repo>/chromium/src), so
that a Chromium roll cannot change what an agent can reach unnoticed:

  * every protocol domain is either fully listed in the rules (every method of
    it has an explicit rule, even a deny) or in the denied domains, never both
    and never neither;
  * every rule names a method that exists in the protocol, and every denied
    domain exists;
  * the rules and the denied domains are sorted and unique (the C++ also
    static_asserts this), and every row between the markers parsed (a row the
    parser silently skipped would still be in the C++);
  * the parameters of every forwarded method (allow, gated, no-op, facade) have
    not changed since scripts/agent/policy_baseline.json was written: a roll that
    adds a parameter to Page.createIsolatedWorld (like grantUniveralAccess) or to
    Input.dispatchKeyEvent (like commands) must be reviewed, not inherited;
  * the events of every listed domain have not changed: a new Network event
    could carry cookies, and the event allowlist and scrubber must decide.

Exit 0 if consistent; 1 and a list of problems otherwise. After reviewing a
change, `--update-baseline` rewrites the baseline file (commit it with the
review's outcome). `--list` prints the unclassified methods as ready-to-paste
`kDeny` rows. Run it on every Chromium roll (scripts/update_chromium.sh does,
and CI does against the pinned version, scripts/ci/check_agent_policy.sh).
"""

import argparse
import json
import os
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import pdl  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
POLICY = ROOT / 'src/browser/agent/koan_agent_policy.cc'
BASELINE = pathlib.Path(__file__).resolve().parent / 'policy_baseline.json'

_RULE = re.compile(
    r'\{"([A-Za-z0-9]+\.[A-Za-z0-9]+)",\s*Verdict::k(\w+),\s*'
    r'Capability::k(\w+),\s*ParamFilter::k(\w+)\}')
_STRING = re.compile(r'"([A-Za-z0-9]+)"')


def _between(text, begin, end):
  start = text.index(begin) + len(begin)
  return text[start:text.index(end, start)]


def parse_policy(text):
  """([(method, verdict, gate, filter), ...], [denied domain, ...]) from C++.

  Raises ValueError if a row between the markers did not parse."""
  region = _between(text, '// BEGIN RULES', '// END RULES')
  rules = _RULE.findall(region)
  rows = region.count('{"')
  if rows != len(rules):
    raise ValueError(f'{rows} rows between the markers, {len(rules)} parsed: '
                     f'a row is malformed')
  denied = _STRING.findall(
      _between(text, '// BEGIN DENIED DOMAINS', '// END DENIED DOMAINS'))
  return rules, denied


def check(protocol, rules, denied):
  """Problems found, as a list of strings. `protocol` is {domain: [command]}."""
  problems = []
  methods = [r[0] for r in rules]
  if methods != sorted(methods) or len(set(methods)) != len(methods):
    problems.append('the rules are not sorted by method name or have duplicates')
  if denied != sorted(denied) or len(set(denied)) != len(denied):
    problems.append('the denied domains are not sorted or have duplicates')

  rule_domains = {}
  for method in methods:
    domain, command = method.split('.')
    rule_domains.setdefault(domain, set()).add(command)

  for domain in sorted(set(rule_domains) & set(denied)):
    problems.append(f'{domain} is both listed in the rules and denied as a domain')
  for domain in sorted(set(protocol) - set(rule_domains) - set(denied)):
    problems.append(f'domain {domain} is not classified (add it to the denied '
                    f'domains or list its methods)')
  for domain in sorted(set(denied) - set(protocol)):
    problems.append(f'denied domain {domain} does not exist in the protocol')
  for domain in sorted(rule_domains):
    if domain not in protocol:
      problems.append(f'rules name domain {domain}, which does not exist')
      continue
    known = set(protocol[domain])
    for command in sorted(known - rule_domains[domain]):
      problems.append(f'{domain}.{command} is in the protocol but has no rule')
    for command in sorted(rule_domains[domain] - known):
      problems.append(f'{domain}.{command} has a rule but is not in the protocol')
  return problems


def snapshot(details, rules):
  """What the baseline records: schema hashes of the forwarded methods, and
  the event names of every listed domain."""
  listed = {r[0].split('.')[0] for r in rules}
  params = {}
  for method, verdict, _, _ in rules:
    if verdict == 'Deny':
      continue
    domain, command = method.split('.')
    params[method] = details.get(domain, {}).get('commands', {}).get(command, '')
  events = {d: sorted(details.get(d, {}).get('events', {})) for d in sorted(listed)}
  return {'params': dict(sorted(params.items())), 'events': events}


def check_baseline(current, baseline):
  """Problems from comparing a snapshot with the committed baseline."""
  problems = []
  for method in sorted(set(current['params']) | set(baseline['params'])):
    old = baseline['params'].get(method)
    new = current['params'].get(method)
    if old is None:
      problems.append(f'{method} is forwarded but is not in the baseline '
                      f'(review it, then --update-baseline)')
    elif new is None:
      problems.append(f'{method} is in the baseline but no longer forwarded')
    elif old != new:
      problems.append(f'the parameters or results of {method} changed: review '
                      f'the filter it needs, then --update-baseline')
  for domain in sorted(set(current['events']) | set(baseline['events'])):
    old = set(baseline['events'].get(domain, []))
    new = set(current['events'].get(domain, []))
    for event in sorted(new - old):
      problems.append(f'new event {domain}.{event}: decide whether the event '
                      f'allowlist passes it and whether it needs scrubbing, '
                      f'then --update-baseline')
    for event in sorted(old - new):
      problems.append(f'event {domain}.{event} no longer exists '
                      f'(--update-baseline)')
  return problems


def unclassified_rows(protocol, rules, denied):
  listed = {r[0] for r in rules}
  rows = []
  for domain in sorted(protocol):
    if domain in denied:
      continue
    if not any(m.startswith(domain + '.') for m in listed):
      continue
    for command in protocol[domain]:
      if f'{domain}.{command}' not in listed:
        rows.append(f'    {{"{domain}.{command}", Verdict::kDeny, '
                    f'Capability::kNone, ParamFilter::kNone}},')
  return rows


def main(argv):
  parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
  parser.add_argument('--chromium-src')
  parser.add_argument('--list', action='store_true',
                      help='print kDeny rows for unclassified methods of '
                      'listed domains')
  parser.add_argument('--update-baseline', action='store_true',
                      help='rewrite policy_baseline.json from the checkout')
  args = parser.parse_args(argv)
  src = (args.chromium_src or os.environ.get('KOAN_CHROMIUM_SRC') or
         str(ROOT / 'chromium/src'))
  if not (pathlib.Path(src) / pdl.PROTOCOL_FILES[0]).exists():
    print(f'no Chromium checkout at {src} (--chromium-src or '
          f'KOAN_CHROMIUM_SRC)', file=sys.stderr)
    return 2
  details = pdl.load_details(src)
  protocol = {d: sorted(p['commands']) for d, p in details.items()}
  try:
    rules, denied = parse_policy(POLICY.read_text())
  except ValueError as error:
    print(f'{POLICY.relative_to(ROOT)}: {error}')
    return 1
  if args.list:
    print('\n'.join(unclassified_rows(protocol, rules, denied)))
    return 0
  current = snapshot(details, rules)
  if args.update_baseline:
    BASELINE.write_text(json.dumps(current, indent=1, sort_keys=True) + '\n')
    print(f'wrote {BASELINE.relative_to(ROOT)}: {len(current["params"])} '
          f'forwarded methods, events of {len(current["events"])} domains')
    return 0
  problems = check(protocol, rules, denied)
  if BASELINE.exists():
    problems += check_baseline(current, json.loads(BASELINE.read_text()))
  else:
    problems.append(f'{BASELINE.relative_to(ROOT)} is missing '
                    f'(--update-baseline)')
  if problems:
    print('\n'.join(problems))
    print(f'\n{len(problems)} problem(s). Fix {POLICY.relative_to(ROOT)} or '
          f'review and --update-baseline.')
    return 1
  commands = sum(len(c) for c in protocol.values())
  print(f'ok: {len(protocol)} domains, {commands} methods; {len(rules)} rules '
        f'in {len({r[0].split(".")[0] for r in rules})} domains, '
        f'{len(denied)} domains denied; {len(current["params"])} forwarded '
        f'methods and the events of {len(current["events"])} domains match the '
        f'baseline')
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
