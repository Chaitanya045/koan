#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Reads the DevTools protocol commands out of Chromium's PDL files.

The protocol's source of truth is third_party/blink/public/devtools_protocol/
browser_protocol.pdl (which includes domains/*.pdl) and v8/include/js_protocol.pdl.
Only the structure needed here is parsed: `domain <Name>` at column 0 (with
optional `experimental` / `deprecated` modifiers) and `command <name>` at
column 2 (same modifiers).
"""

import pathlib
import re

_MODIFIERS = r'(?:(?:experimental|deprecated)\s+)*'
_DOMAIN = re.compile(r'^' + _MODIFIERS + r'domain\s+(\w+)\s*$')
_COMMAND = re.compile(r'^  ' + _MODIFIERS + r'command\s+(\w+)\s*$')
_INCLUDE = re.compile(r'^include\s+(\S+)\s*$')

PROTOCOL_FILES = (
    'third_party/blink/public/devtools_protocol/browser_protocol.pdl',
    'v8/include/js_protocol.pdl',
)


def parse_pdl_text(text):
  """Returns {domain: [command, ...]} for one PDL file's text."""
  domains = {}
  current = None
  for line in text.splitlines():
    m = _DOMAIN.match(line)
    if m:
      current = m.group(1)
      domains.setdefault(current, [])
      continue
    m = _COMMAND.match(line)
    if m and current:
      domains[current].append(m.group(1))
  return domains


def parse_pdl_file(path, seen=None):
  """Parses a PDL file and the files it includes, depth first."""
  path = pathlib.Path(path)
  seen = set() if seen is None else seen
  if path in seen:
    return {}
  seen.add(path)
  result = {}
  text = path.read_text()
  for line in text.splitlines():
    m = _INCLUDE.match(line)
    if m:
      for domain, commands in parse_pdl_file(path.parent / m.group(1),
                                             seen).items():
        result.setdefault(domain, []).extend(commands)
  for domain, commands in parse_pdl_text(text).items():
    result.setdefault(domain, []).extend(commands)
  return result


def load_protocol(chromium_src):
  """{domain: sorted commands} of the whole protocol in a Chromium checkout."""
  src = pathlib.Path(chromium_src)
  protocol = {}
  for rel in PROTOCOL_FILES:
    for domain, commands in parse_pdl_file(src / rel).items():
      protocol.setdefault(domain, []).extend(commands)
  return {d: sorted(set(c)) for d, c in protocol.items()}
