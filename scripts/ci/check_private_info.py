#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Fails if a tracked text file names a private machine (CI job "Private details").

The repository is public. The Mac bench's host name, address, user name and ssh
key file must live in the environment or in the git-ignored scripts/mac/local.env
(scripts/mac/local.env.example), and notes say <mac-host>, <mac-ip>, <user> and
<ssh-key>. This looks for what leaks them, in every tracked text file:

  - a `.local` (mDNS) host name, e.g. `my-mac.local`;
  - a hyphenated host name with `macbook` in it (`name-macbook-air`);
  - a private-range IPv4 address (10/8, 172.16/12, 192.168/16);
  - an ssh key path (`.ssh/<file>`), or a `*_ed25519`/`*_rsa`/`*_ecdsa` file name.

Placeholders in angle brackets never match. A line that is meant to show one of
these (this script's own docs, a test) is allowed by `koan-allow-private` on it,
or by a file glob in scripts/ci/private_info_allowlist.txt. Exits 1 and lists
file:line:match otherwise. Git history is not looked at.

  check_private_info.py [--list-allowlist]
"""

import fnmatch
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
ALLOWLIST = 'scripts/ci/private_info_allowlist.txt'
INLINE_MARK = 'koan-allow-private'

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
}


def allowed_globs(text):
  return [l.strip() for l in text.splitlines()
          if l.strip() and not l.lstrip().startswith('#')]


def scan_text(path, text):
  """[(line number, kind, match)] of what leaks in one file's text."""
  found = []
  for number, line in enumerate(text.splitlines(), 1):
    if INLINE_MARK in line:
      continue
    for kind, pattern in PATTERNS.items():
      for match in pattern.finditer(line):
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


def check(files, globs):
  """[(file, line, kind, match)] over (name, text) pairs, minus the allowlist."""
  problems = []
  for name, text in files:
    if any(fnmatch.fnmatch(name, g) for g in globs):
      continue
    problems += [(name, n, kind, m) for n, kind, m in scan_text(name, text)]
  return problems


def main():
  allow = ROOT / ALLOWLIST
  globs = allowed_globs(allow.read_text()) if allow.exists() else []
  if '--list-allowlist' in sys.argv:
    print('\n'.join(globs))
    return 0
  problems = check(tracked_text_files(), globs)
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
