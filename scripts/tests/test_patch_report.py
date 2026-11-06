#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Tests for scripts/ci/patch_report.py (patch health, REBASE.md rows)."""

import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0,
                str(pathlib.Path(__file__).resolve().parent.parent / 'ci'))
import patch_report as pr  # noqa: E402

PATCH = '''diff --git a/a/b.cc b/a/b.cc
index 1..2 100644
--- a/a/b.cc
+++ b/a/b.cc
@@ -1,3 +1,4 @@
 context
-old
+new
+more
@@ -10,2 +11,2 @@
 context
+one
'''

ROWS = '''| Hook | File | Check |
|---|---|---|
| Tab menu | `chrome/browser/ui/tabs/tab_menu_model.cc` (one hook) | ... |
| Colours | `ui/color/color_mixers.{h,cc}` | ... |
| Shorthand | `.../tabs/common/tab_view.cc` | ... |
Prose mentioning other_file.cc is not a row.
'''


class SizeTest(unittest.TestCase):

  def test_counts_changed_lines_and_hunks_not_headers(self):
    self.assertEqual(pr.patch_size(PATCH), (4, 2))

  def test_upstream_path(self):
    self.assertEqual(pr.upstream_path('patches/chrome/a/b.cc.patch'),
                     'chrome/a/b.cc')


class RowTest(unittest.TestCase):

  def rows(self):
    return pr.table_rows(ROWS)

  def test_only_table_lines_count(self):
    self.assertNotIn('other_file.cc', self.rows())

  def test_path_name_brace_and_shorthand_match(self):
    paths = [
        'chrome/browser/ui/tabs/tab_menu_model.cc', 'ui/color/color_mixers.h',
        'ui/color/color_mixers.cc', 'chrome/browser/ui/views/tabs/common/tab_view.cc'
    ]
    self.assertEqual(pr.missing_rows(paths, paths, self.rows()), [])

  def test_a_patch_with_no_row_is_missing(self):
    paths = ['chrome/a/new_hook.cc', 'chrome/browser/ui/tabs/tab_menu_model.cc']
    self.assertEqual(pr.missing_rows(paths, paths, self.rows()),
                     ['chrome/a/new_hook.cc'])

  def test_a_build_file_is_covered_by_a_sibling_with_a_row(self):
    tabs = 'chrome/browser/ui/tabs/BUILD.gn'
    other = 'chrome/other/BUILD.gn'
    everything = [tabs, other, 'chrome/browser/ui/tabs/tab_menu_model.cc']
    self.assertEqual(pr.missing_rows([tabs, other], everything, self.rows()),
                     [other])
    # ...or by naming its path.
    rows = self.rows() + '\n| x | `chrome/other/BUILD.gn` | y |'
    self.assertEqual(pr.missing_rows([other], everything, rows), [])


class CompareTest(unittest.TestCase):

  def test_new_grown_shrunk_removed(self):
    base = {'a': (5, 1), 'b': (9, 1), 'c': (3, 1)}
    head = {'a': (8, 2), 'b': (2, 1), 'd': (4, 1)}
    new, grown, shrunk, removed = pr.compare(base, head)
    self.assertEqual(new, [('d', None, 4)])
    self.assertEqual(grown, [('a', 5, 8)])
    self.assertEqual(shrunk, [('b', 9, 2)])
    self.assertEqual(removed, [('c', 3, None)])


class RenderTest(unittest.TestCase):

  def test_without_a_base_only_totals(self):
    report, missing = pr.render({'a': (5, 1)}, None, '', None)
    self.assertIn('1 patches, 5 changed upstream lines', report)
    self.assertEqual(missing, [])

  def test_fails_for_a_new_patch_without_a_row_not_for_old_ones(self):
    base = {'old/no_row.cc': (3, 1)}
    head = {'old/no_row.cc': (3, 1), 'new/hook.cc': (6, 1)}
    report, missing = pr.render(head, base, '| x | `hook.cc` | y |', 'origin/develop')
    self.assertEqual(missing, [])  # new/hook.cc has a row by file name
    self.assertIn('+1', report)
    report, missing = pr.render(head, base, '', 'origin/develop')
    self.assertEqual(missing, ['new/hook.cc'])
    self.assertIn('FAIL', report)
    self.assertIn('`new/hook.cc`: 6 lines', report)

  def test_large_patches_are_listed_but_do_not_fail(self):
    head = {'a/big.cc': (pr.LARGE_LINES, 3)}
    report, missing = pr.render(head, dict(head), '', 'b')
    self.assertIn('Large patches', report)
    self.assertEqual(missing, [])


class RepositoryTest(unittest.TestCase):

  def test_every_patch_has_a_row_in_the_hook_table(self):
    head = pr.sizes_on_disk()
    rows = pr.table_rows((pr.ROOT / pr.REBASE_MD).read_text())
    self.assertEqual(pr.missing_rows(list(head), head, rows), [])


class GitTest(unittest.TestCase):
  """The whole thing on a throwaway repository."""

  def git(self, *args):
    subprocess.run(['git', '-c', 'user.name=t', '-c', 'user.email=t@t', *args],
                   cwd=self.root, check=True, capture_output=True)

  def test_a_pr_adding_a_patch_without_a_row_fails(self):
    with tempfile.TemporaryDirectory() as tmp:
      self.root = pathlib.Path(tmp)
      (self.root / 'patches/a').mkdir(parents=True)
      (self.root / 'docs').mkdir()
      (self.root / 'patches/a/old.cc.patch').write_text(PATCH)
      (self.root / 'docs/REBASE.md').write_text('| Old | `old.cc` | x |\n')
      self.git('init', '-q')
      self.git('add', '-A')
      self.git('commit', '-qm', 'base')
      (self.root / 'patches/a/new_hook.cc.patch').write_text(PATCH)
      saved = pr.ROOT
      pr.ROOT = self.root
      try:
        with self.assertRaises(SystemExit) as raised:
          sys.exit(pr.main(['--base', 'HEAD']))
        self.assertEqual(raised.exception.code, 1)
        (self.root / 'docs/REBASE.md').write_text(
            '| Old | `old.cc` | x |\n| New | `a/new_hook.cc` | y |\n')
        self.assertEqual(pr.main(['--base', 'HEAD']), 0)
      finally:
        pr.ROOT = saved


if __name__ == '__main__':
  unittest.main()
