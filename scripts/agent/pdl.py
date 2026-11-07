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

import hashlib
import pathlib
import re

_MODIFIERS = r'(?:(?:experimental|deprecated)\s+)*'
_DOMAIN = re.compile(r'^' + _MODIFIERS + r'domain\s+(\w+)\s*$')
_COMMAND = re.compile(r'^  ' + _MODIFIERS + r'command\s+(\w+)\s*$')
_EVENT = re.compile(r'^  ' + _MODIFIERS + r'event\s+(\w+)\s*$')
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


def _block_hash(lines):
  """Hash of a definition's body with comments, blank lines and trailing
  spaces removed, so that rewording a doc comment does not change it."""
  kept = []
  for line in lines:
    stripped = line.rstrip()
    if not stripped or stripped.lstrip().startswith('#'):
      continue
    kept.append(stripped)
  return hashlib.sha256('\n'.join(kept).encode()).hexdigest()


def parse_details_text(text):
  """{domain: {'commands': {name: hash}, 'events': {name: hash}}}.

  The hash covers the command's (or event's) parameters and returns, i.e.
  every line indented more than the definition itself, comments excluded.
  """
  domains = {}
  current = None
  kind = name = None
  body = []

  def flush():
    if current and kind and name:
      domains[current][kind][name] = _block_hash(body)

  for line in text.splitlines():
    m = _DOMAIN.match(line)
    if m:
      flush()
      kind = name = None
      body = []
      current = m.group(1)
      domains.setdefault(current, {'commands': {}, 'events': {}})
      continue
    m = _COMMAND.match(line) or _EVENT.match(line)
    if m and current:
      flush()
      kind = 'commands' if _COMMAND.match(line) else 'events'
      name = m.group(1)
      body = []
      continue
    if name:
      indent = len(line) - len(line.lstrip(' '))
      if line.strip() and indent <= 2:
        flush()
        kind = name = None
        body = []
      else:
        body.append(line)
  flush()
  return domains


def parse_details_file(path, seen=None):
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
      for domain, parts in parse_details_file(path.parent / m.group(1),
                                              seen).items():
        merged = result.setdefault(domain, {'commands': {}, 'events': {}})
        merged['commands'].update(parts['commands'])
        merged['events'].update(parts['events'])
  for domain, parts in parse_details_text(text).items():
    merged = result.setdefault(domain, {'commands': {}, 'events': {}})
    merged['commands'].update(parts['commands'])
    merged['events'].update(parts['events'])
  return result


def load_details(chromium_src):
  """Like load_protocol, with each command's and event's schema hash."""
  src = pathlib.Path(chromium_src)
  details = {}
  for rel in PROTOCOL_FILES:
    for domain, parts in parse_details_file(src / rel).items():
      merged = details.setdefault(domain, {'commands': {}, 'events': {}})
      merged['commands'].update(parts['commands'])
      merged['events'].update(parts['events'])
  return details


# --- Schemas: the types an event or command refers to ------------------------
#
# A field added three types down in Network.Request is as reachable by an agent
# as one added to the event itself, so the drift gate hashes an event with every
# type it refers to, and walks the same closure for secret-looking fields.

_BUILTIN_TYPES = {'string', 'integer', 'number', 'boolean', 'object', 'any',
                  'binary', 'enum'}
_FIELD_MODIFIERS = {'optional', 'experimental', 'deprecated'}
# `type X extends T` and `type X extends array of T`: the type T refers to.
_EXTENDS = re.compile(r'\bextends\s+(?:array\s+of\s+)?(\S+)\s*$')
_DEFINITION = re.compile(r'^  ' + _MODIFIERS + r'(type|command|event)\s+(\w+)\b')


def _parse_field(line):
  """(name, type reference or None) of a PDL field line, or None if the line is
  not a field (an enum value, a keyword such as `properties`)."""
  tokens = [t for t in line.split() if t not in _FIELD_MODIFIERS]
  if len(tokens) < 2:
    return None
  if tokens[0] == 'array' and len(tokens) >= 4 and tokens[1] == 'of':
    base = tokens[2]
  else:
    base = tokens[0]
  return tokens[-1], (None if base in _BUILTIN_TYPES else base)


def parse_schema_text(text):
  """{'Domain.Name': {'kind': 'type'|'command'|'event', 'hash': h,
  'fields': [(name, 'Domain.Type' or None), ...]}} for one PDL file's text.
  A type reference without a domain is qualified with the domain it is in."""
  blocks = {}
  domain = None
  current = None  # (kind, name, body lines)

  def flush():
    if current and domain:
      kind, name, body = current
      fields = []
      extends = _EXTENDS.search(body[0])
      if extends and extends.group(1) not in _BUILTIN_TYPES:
        ref = extends.group(1)
        fields.append(('<extends>', ref if '.' in ref else f'{domain}.{ref}'))
      for line in body[1:]:
        stripped = line.strip()
        indent = len(line) - len(line.lstrip(' '))
        if not stripped or stripped.startswith('#') or indent != 6:
          continue
        parsed = _parse_field(stripped)
        if parsed:
          field, ref = parsed
          if ref and '.' not in ref:
            ref = f'{domain}.{ref}'
          fields.append((field, ref))
      blocks[f'{domain}.{name}'] = {'kind': kind, 'hash': _block_hash(body),
                                    'fields': fields}

  for line in text.splitlines():
    m = _DOMAIN.match(line)
    if m:
      flush()
      current = None
      domain = m.group(1)
      continue
    m = _DEFINITION.match(line)
    if m and domain:
      flush()
      # The header line is part of the definition: `type X extends string`
      # retargeted to `extends array of Network.Cookie` changes what X is.
      current = (m.group(1), m.group(2), [line])
      continue
    if current:
      indent = len(line) - len(line.lstrip(' '))
      if line.strip() and indent <= 2:
        flush()
        current = None
      else:
        current[2].append(line)
  flush()
  return blocks


def parse_schema_file(path, seen=None):
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
      result.update(parse_schema_file(path.parent / m.group(1), seen))
  result.update(parse_schema_text(text))
  return result


def load_schema(chromium_src):
  src = pathlib.Path(chromium_src)
  schema = {}
  for rel in PROTOCOL_FILES:
    schema.update(parse_schema_file(src / rel))
  return schema


def closure(schema, root, drop=None):
  """The root definition ('Domain.event') and every type it refers to,
  transitively, as a sorted list of keys. Unknown references are kept (and
  flagged by the caller) rather than skipped. `drop(field name)`, if given,
  says that a field is removed before delivery: its type is not followed (a
  type that is only reachable through removed fields is not reachable)."""
  seen = []
  todo = [root]
  while todo:
    key = todo.pop()
    if key in seen:
      continue
    seen.append(key)
    for name, ref in schema.get(key, {}).get('fields', []):
      if ref and not (drop and drop(name)):
        todo.append(ref)
  return sorted(seen)


def closure_hash(schema, root):
  """One hash for a definition and everything it refers to."""
  parts = []
  for key in closure(schema, root):
    block = schema.get(key)
    parts.append(f'{key}:{block["hash"] if block else "MISSING"}')
  return hashlib.sha256('\n'.join(parts).encode()).hexdigest()


def closure_fields(schema, root, drop=None):
  """[(owner, field, type reference or None)] for every field reachable from
  the root, sorted; fields `drop` names are left out."""
  fields = []
  for key in closure(schema, root, drop):
    for name, ref in schema.get(key, {}).get('fields', []):
      if not (drop and drop(name)):
        fields.append((key, name, ref))
  return sorted(fields, key=lambda f: (f[0], f[1]))
