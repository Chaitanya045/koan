#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Static checks of the user flows (scripts/mac/video/flows/uf_*.mjs), which
only run on the Mac bench: a name a flow imports that its module doesn't
export would only show there, as a crash before the first step. Also: every
flow is in run_flows.sh's list, in the README's table, and has its test
cases in .agent/notes/user-flows.md, and every test case ID a flow runs is
in the catalogue.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
VIDEO = ROOT / 'scripts' / 'mac' / 'video'
FLOWS = sorted((VIDEO / 'flows').glob('uf_*.mjs'))
CATALOGUE = ROOT / '.agent' / 'notes' / 'user-flows.md'


def exports(path):
  src = path.read_text()
  names = set(re.findall(r'^export\s+(?:async\s+)?(?:function\*?|const|let|var|class)\s+([A-Za-z_$][\w$]*)', src, re.M))
  for m in re.finditer(r'^export\s+const\s+[A-Za-z_$][\w$]*\s*=\s*[^,;]+((?:,\s*[A-Za-z_$][\w$]*\s*=\s*[^,;]+)+);', src, re.M):
    names.update(re.findall(r',\s*([A-Za-z_$][\w$]*)\s*=', m.group(1)))
  for m in re.finditer(r'^export\s*\{([^}]*)\}', src, re.M):
    for n in m.group(1).split(','):
      n = n.strip().split(' as ')[-1].strip()
      if n:
        names.add(n)
  return names


class FlowImportsTest(unittest.TestCase):

  def test_flows_exist(self):
    self.assertGreaterEqual(len(FLOWS), 10)

  def test_named_imports_are_exported(self):
    missing = []
    for flow in [*FLOWS, VIDEO / 'flows' / 'uf_lib.mjs', VIDEO / 'flows' / 'common.mjs']:
      for m in re.finditer(r"import\s*\{([^}]*)\}\s*from\s*'(\.[^']+)'", flow.read_text()):
        target = (flow.parent / m.group(2)).resolve()
        have = exports(target)
        for n in m.group(1).split(','):
          n = n.strip().split(' as ')[0].strip()
          if n and n not in have:
            missing.append(f'{flow.name}: {n} from {m.group(2)}')
    self.assertEqual(missing, [])

  def test_every_flow_is_wired(self):
    runner = (VIDEO / 'run_flows.sh').read_text()
    readme = (VIDEO / 'README.md').read_text()
    catalogue = CATALOGUE.read_text()
    listed = re.search(r'^ALL=\(([^)]*)\)', runner, re.M).group(1).split()
    for flow in FLOWS:
      name = flow.stem
      if name == 'uf_lib':
        continue
      self.assertIn(name, listed, f'{name} is not in run_flows.sh ALL')
      self.assertIn(f'flows/{name}.mjs', readme, f'{name} is not in the README table')
      self.assertIn(f'{name}', catalogue, f'{name} is not in user-flows.md')

  def test_step_ids_are_in_the_catalogue(self):
    catalogue = CATALOGUE.read_text()
    known = set(re.findall(r'\bUF-[A-Z]+-\d+\b', catalogue))
    unknown = []
    for flow in FLOWS:
      for sid in re.findall(r"['`](UF-[A-Z]+-\d+)", flow.read_text()):
        if sid not in known:
          unknown.append(f'{flow.name}: {sid}')
    self.assertEqual(unknown, [])


if __name__ == '__main__':
  unittest.main()
