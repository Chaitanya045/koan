#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Fails if a tracked text file names a private machine (CI job "Private details").

The repository is public. The Mac bench's host name, address, user name and ssh
key file must live in the environment or in the git-ignored scripts/mac/local.env
(scripts/mac/local.env.example), and notes say <mac-host>, <mac-ip>, <user> and
<ssh-key>. This looks for what leaks them, in every tracked text file:

  - a `.local` (mDNS) host name, e.g. `example-host.local`;
  - a hyphenated host name with `macbook` in it (`test-host-macbook-air`);
  - a private-range IPv4 address (10/8, 172.16/12, 192.168/16), e.g. `10.0.0.1`;
  - an ssh key path (`.ssh/<file>`), or a private-key file name (an underscore
    and `ed25519`, `rsa` or `ecdsa`), e.g. `test_key_ed25519`;
  - a Mac home folder with a real user name: `/Users/<name>` (`/Users/<user>`,
    `/Users/${...}` and `/Users/$HOME` are placeholders).

Placeholders in angle brackets never match. What the allowlist
(scripts/ci/private_info_allowlist.txt) names is a list of exact TOKENS that may
appear, obviously fake ones (`example-host.local`, `10.0.0.1`,
`test_key_ed25519`, `/Users/Shared`...): never a file, never a line. A test
that needs a matching string builds it at runtime. Exits 1 and lists
file:line:match otherwise. Git history is not looked at.

  check_private_info.py
"""

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
ALLOWLIST = 'scripts/ci/private_info_allowlist.txt'

PATTERNS = {
    '.local host name': re.compile(
        r'(?<![\w./~-])[A-Za-z0-9][A-Za-z0-9-]*\.local\b(?![/\w-])'),
    'macbook host name': re.compile(
        r'\b[a-z0-9]+(?:-[a-z0-9]+)*-macbook(?:-[a-z0-9]+)*\b', re.I),
    'private IP address': re.compile(
        r'(?<![\d.])(?:10\.\d{1,3}\.\d{1,3}\.\d{1,3}'
        r'|192\.168\.\d{1,3}\.\d{1,3}'
        r'|172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3})(?![\d])'),
    'ssh key path': re.compile(
        r'\.ssh/(?!<)[\w.-]+|\b[\w.-]*_(?:ed25519|rsa|ecdsa)\b'),
    'Mac user folder': re.compile(r'/Users/(?![<$])[\w.-]+'),
}


def allowed_tokens(text):
  """The allowlist: one exact token per line, # comments."""
  return {l.strip() for l in text.splitlines()
          if l.strip() and not l.lstrip().startswith('#')}


def scan_text(text, tokens=frozenset()):
  """[(line number, kind, match)] of what leaks in one file's text, except the
  allowed tokens."""
  found = []
  for number, line in enumerate(text.splitlines(), 1):
    for kind, pattern in PATTERNS.items():
      for match in pattern.finditer(line):
        if match.group(0) not in tokens:
          found.append((number, kind, match.group(0)))
  return found


def tracked_text_files():
  out = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, capture_output=True,
                       check=True).stdout.decode()
  for name in filter(None, out.split('\0')):
    path = ROOT / name
    if not path.is_file() or path.is_symlink():
      continue
    data = path.read_bytes()
    if b'\0' in data:
      continue  # binary
    try:
      yield name, data.decode('utf-8')
    except UnicodeDecodeError:
      continue


def check(files, tokens):
  """[(file, line, kind, match)] over (name, text) pairs, minus the allowed
  tokens."""
  problems = []
  for name, text in files:
    problems += [(name, n, kind, m) for n, kind, m in scan_text(text, tokens)]
  return problems


def main():
  tokens = allowed_tokens((ROOT / ALLOWLIST).read_text())
  if '--list-allowlist' in sys.argv:
    print('\n'.join(sorted(tokens)))
    return 0
  problems = check(tracked_text_files(), tokens)
  for name, number, kind, match in problems:
    print('%s:%d: %s: %s' % (name, number, kind, match))
  if problems:
    print('\n%d private detail(s) in tracked files. Keep them in the environment '
          'or scripts/mac/local.env, and write <mac-host>, <mac-ip>, <user>, '
          '<ssh-key> in notes (scripts/mac/README.md). Git history is not '
          'rewritten by this check.' % len(problems), file=sys.stderr)
    return 1
  print('no private host names, addresses or key paths in tracked text files')
  return 0


if __name__ == '__main__':
  sys.exit(main())
