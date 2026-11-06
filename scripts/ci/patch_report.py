#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Patch health of a change to Koan's hook patches (.github/workflows/ci.yml).

Every patch in patches/ is a line of upstream Chromium that Koan changes, and
a place the next Chromium release can break. This reports, for the working
tree against a base revision (the PR's merge base):

  - the number of patches and the total of changed upstream lines;
  - new, grown, shrunk and removed patches with their size;
  - patches with a large number of changed lines (informational).

It fails (exit 1) if a NEW patch has no row in docs/REBASE.md: a row in the
hook table says what the patch hooks, why, and what to re-check at a rebase
(rule 2 of "Upgrade-safe code" in .agent/notes/agent-brief.md). A row is a
table line (starting with `|`) that names the patch's file: by its path, or
its file name; for a BUILD.gn/.gni by its path or, as the rows mention them
("+ its BUILD.gn"), when another patch in the same directory has a row.

  patch_report.py [--base REF] [--summary FILE]

Without --base only the totals are reported. --summary appends the markdown
to FILE (GITHUB_STEP_SUMMARY); it is always printed.
"""

import argparse
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
PATCHES = 'patches'
REBASE_MD = 'docs/REBASE.md'
# Informational: a patch this big is probably more than a hook.
LARGE_LINES = 40
BUILD_FILES = ('BUILD.gn', 'BUILD.gni')


def patch_size(text):
  """(changed lines, hunks) of a patch: the lines it adds and removes."""
  changed = 0
  hunks = 0
  in_hunk = False
  for line in text.splitlines():
    if line.startswith('@@'):
      hunks += 1
      in_hunk = True
    elif in_hunk and line[:1] in ('+', '-'):
      changed += 1
    elif line.startswith('diff --git'):
      in_hunk = False
  return changed, hunks


def upstream_path(patch_file):
  """patches/a/b.cc.patch -> a/b.cc"""
  rel = patch_file[len(PATCHES) + 1:]
  return rel[:-len('.patch')] if rel.endswith('.patch') else rel


def git(*args):
  return subprocess.run(['git', *args], cwd=ROOT, capture_output=True,
                        text=True, check=True).stdout


def sizes_at(ref):
  """{upstream path: (changed lines, hunks)} of the patches at a revision."""
  out = git('ls-tree', '-r', '--name-only', ref, '--', PATCHES)
  result = {}
  for name in out.splitlines():
    if name.endswith('.patch'):
      result[upstream_path(name)] = patch_size(git('show', '%s:%s' % (ref, name)))
  return result


def sizes_on_disk():
  result = {}
  for path in sorted((ROOT / PATCHES).rglob('*.patch')):
    name = str(path.relative_to(ROOT))
    result[upstream_path(name)] = patch_size(path.read_text(errors='replace'))
  return result


def table_rows(markdown):
  """The table lines of REBASE.md, joined: only they count as rows. A
  `name.{h,cc}` also counts as `name.h` and `name.cc`."""
  rows = '\n'.join(
      l for l in markdown.splitlines() if l.lstrip().startswith('|'))
  expanded = [
      '%s.%s' % (base, ext)
      for base, exts in re.findall(r'([\w./-]+)\.\{([\w,]+)\}', rows)
      for ext in exts.split(',')
  ]
  return rows + '\n' + '\n'.join(expanded)


def has_row(path, rows, covered_dirs):
  """Does a hook-table row name this patch's file?"""
  name = path.rsplit('/', 1)[-1]
  directory = path.rsplit('/', 1)[0] if '/' in path else ''
  if path in rows:
    return True
  if name in BUILD_FILES:
    return directory in covered_dirs
  return name in rows


def missing_rows(new_paths, all_paths, rows):
  """The new patches no row names. A BUILD file is covered when another
  patch in its directory (new or not) has a row."""
  covered_dirs = {
      p.rsplit('/', 1)[0] if '/' in p else ''
      for p in all_paths
      if p.rsplit('/', 1)[-1] not in BUILD_FILES and has_row(p, rows, set())
  }
  return sorted(p for p in new_paths if not has_row(p, rows, covered_dirs))


def compare(base, head):
  """New, grown, shrunk and removed patches: lists of (path, before, after)
  changed-line counts (None for absent)."""
  new = [(p, None, head[p][0]) for p in sorted(head) if p not in base]
  removed = [(p, base[p][0], None) for p in sorted(base) if p not in head]
  grown = [(p, base[p][0], head[p][0]) for p in sorted(head)
           if p in base and head[p][0] > base[p][0]]
  shrunk = [(p, base[p][0], head[p][0]) for p in sorted(head)
            if p in base and head[p][0] < base[p][0]]
  return new, grown, shrunk, removed


def render(head, base, rows, base_ref):
  """(markdown, failures): the report, and the new patches without a row."""
  total = sum(c for c, _ in head.values())
  lines = ['## Patch health', '']
  if base is None:
    lines.append('%d patches, %d changed upstream lines (no base to compare '
                 'with).' % (len(head), total))
    return '\n'.join(lines) + '\n', []
  base_total = sum(c for c, _ in base.values())
  lines += [
      '| | base (`%s`) | this change | |' % base_ref,
      '|---|---|---|---|',
      '| patches | %d | %d | %+d |' % (len(base), len(head), len(head) - len(base)),
      '| changed upstream lines | %d | %d | %+d |' % (base_total, total,
                                                       total - base_total),
      '',
  ]
  new, grown, shrunk, removed = compare(base, head)
  for title, items in (('New patches', new), ('Grown patches', grown),
                       ('Shrunk patches', shrunk), ('Removed patches', removed)):
    if not items:
      continue
    lines += ['**%s**' % title, '']
    for path, before, after in items:
      size = ('%d lines' % after if before is None else
              '%d lines (was %d)' % (before, after) if after is None else
              '%d -> %d lines' % (before, after))
      lines.append('- `%s`: %s' % (path, size))
    lines.append('')
  large = [(p, head[p][0]) for p in sorted(head) if head[p][0] >= LARGE_LINES]
  if large:
    lines += ['Large patches (%d+ changed lines): ' % LARGE_LINES + ', '.join(
        '`%s` %d' % (p.rsplit('/', 1)[-1], n) for p, n in large), '']
  missing = missing_rows([p for p, _, _ in new], head, rows) if new else []
  if missing:
    lines += ['**FAIL: a new patch has no row in docs/REBASE.md** (add a row '
              'to the hook table: what it hooks, why no extension point fits, '
              'what to re-check at a rebase):', '']
    lines += ['- `%s`' % p for p in missing] + ['']
  return '\n'.join(lines) + '\n', missing


def main(argv=None):
  parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
  parser.add_argument('--base', help='revision to compare with (merge base)')
  parser.add_argument('--summary', help='file to append the markdown to')
  args = parser.parse_args(argv)

  head = sizes_on_disk()
  base = sizes_at(args.base) if args.base else None
  rows = table_rows((ROOT / REBASE_MD).read_text())
  report, missing = render(head, base, rows, args.base)
  print(report)
  if args.summary:
    with open(args.summary, 'a') as f:
      f.write(report)
  return 1 if missing else 0


if __name__ == '__main__':
  sys.exit(main())
