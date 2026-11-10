#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Checks that every per-profile KeyedService is decided for agent containers.

    scripts/agent/check_agent_services.py [--inventory FILE] [--policy FILE]

Agent containers share the profile's services (.agent/notes/agent-control.md
section 7.2), so each factory in the pinned Chromium needs a row in
src/browser/containers/koan_agent_service_policy.cc: agent-safe, blocked (by
one of the agent blocks) or a known gap. This is the static half of the
guard; koan_agent_service_unittests is the runtime half (it asks the
dependency graph, so it also sees factories a static scan can't name).

It needs no Chromium checkout: it compares the policy with
scripts/agent/keyed_services.json, the inventory list_keyed_services.py wrote
from the pinned revision. It fails (exit 1) when:

  * the inventory is not of the pinned revision (CHROMIUM_VERSION line 2):
    a roll must regenerate it (scripts/update_chromium.sh does);
  * a factory in the inventory has no row (a new upstream service);
  * a row between the markers doesn't parse, or the rows are unsorted or
    duplicated.

Rows for factories the inventory doesn't have are reported (the runtime test
may need them), and so are the known gaps.
"""

import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
POLICY = ROOT / 'src/browser/containers/koan_agent_service_policy.cc'
INVENTORY = pathlib.Path(__file__).resolve().parent / 'keyed_services.json'
VERSION = ROOT / 'CHROMIUM_VERSION'

BEGIN = '// BEGIN AGENT SERVICE RULES'
END = '// END AGENT SERVICE RULES'
_ROW = re.compile(r'\{\s*"([^"\\]+)",\s*ServiceVerdict::k(Safe|Blocked|Gap),\s*'
                  r'"((?:[^"\\]|"\s*")*)"\s*\}')


def parse_policy(text):
  """[(name, verdict, reason)] and a list of problems. A row may be wrapped
  across lines and its reason split into adjacent string literals."""
  if BEGIN not in text or END not in text:
    return [], ['the policy has no %s / %s markers' % (BEGIN, END)]
  body = text.split(BEGIN, 1)[1].split(END, 1)[0]
  rules = [(m.group(1), m.group(2), re.sub(r'"\s*"', '', m.group(3)))
           for m in _ROW.finditer(body)]
  problems = []
  rows = body.count('{')
  if rows != len(rules):
    problems.append('%d rows between the markers, %d parsed' %
                    (rows, len(rules)))
  return rules, problems


def check(rules, inventory, pinned_commit):
  problems, notes = [], []
  if inventory.get('chromium_commit') != pinned_commit:
    problems.append(
        'scripts/agent/keyed_services.json is of %s, the pin is %s: run '
        'scripts/agent/list_keyed_services.py --write against the pinned '
        'checkout' % (inventory.get('chromium_commit'), pinned_commit))
  names = [r[0] for r in rules]
  for a, b in zip(names, names[1:]):
    if a >= b:
      problems.append('rows not sorted or duplicated: %s, %s' % (a, b))
  decided = set(names)
  for name in sorted(inventory.get('factories', {})):
    if name not in decided:
      problems.append('undecided per-profile service %s (%s): add a row' %
                      (name, inventory['factories'][name]))
  extra = sorted(decided - set(inventory.get('factories', {})))
  if extra:
    notes.append('rows not in the static inventory (kept for the runtime '
                 'test): ' + ', '.join(extra))
  gaps = [r for r in rules if r[1] == 'Gap']
  for name, _, reason in gaps:
    notes.append('known gap: %s: %s' % (name, reason))
  return problems, notes


def main():
  parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
  parser.add_argument('--inventory', default=str(INVENTORY))
  parser.add_argument('--policy', default=str(POLICY))
  args = parser.parse_args()
  rules, problems = parse_policy(pathlib.Path(args.policy).read_text())
  inventory = json.loads(pathlib.Path(args.inventory).read_text())
  pinned = VERSION.read_text().split()[1]
  more, notes = check(rules, inventory, pinned)
  problems += more
  for note in notes:
    print('note: ' + note)
  for problem in problems:
    print('error: ' + problem)
  verdicts = {}
  for _, verdict, _ in rules:
    verdicts[verdict] = verdicts.get(verdict, 0) + 1
  print('%d services decided (%s), %d problems' %
        (len(rules), ', '.join('%s %d' % kv for kv in sorted(verdicts.items())),
         len(problems)))
  return 1 if problems else 0


if __name__ == '__main__':
  sys.exit(main())
