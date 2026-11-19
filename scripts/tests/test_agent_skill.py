#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""The koan-browser entry skill has one text in two places.

skills/koan-browser/SKILL.md is what `npx skills add Chaitanya045/koan`
installs (the skills CLI finds skills/<name>/SKILL.md in a repo), and
src/browser/agent/koan_agent_setup.cc writes the same text when the user
clicks Settings > Agent Control > Set Up Agents. These run in CI; the C++
tests run on the Mac.
"""

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
REPO_SKILL = ROOT / 'skills' / 'koan-browser' / 'SKILL.md'
SETUP_CC = ROOT / 'src' / 'browser' / 'agent' / 'koan_agent_setup.cc'


def cc_skill():
    text = SETUP_CC.read_text(encoding='utf-8')
    match = re.search(r'kEntrySkill\[\] = R"\((.*?)\)";', text, re.S)
    assert match, 'no kEntrySkill raw string in koan_agent_setup.cc'
    return match.group(1)


class AgentSkillTest(unittest.TestCase):

    def test_the_app_writes_the_repo_skill(self):
        self.assertEqual(cc_skill(), REPO_SKILL.read_text(encoding='utf-8'))

    def test_front_matter(self):
        text = REPO_SKILL.read_text(encoding='utf-8')
        front = text.split('---\n')[1]
        self.assertIn('name: koan-browser\n', front)
        description = re.search(r'^description: (.+)$', front, re.M)
        self.assertTrue(description)
        # The skills CLI and the agents read one line of description.
        self.assertLess(len(description.group(1)), 1024)

    def test_it_points_at_the_guide(self):
        text = REPO_SKILL.read_text(encoding='utf-8')
        self.assertIn('run `koan skill`', text)
        self.assertIn('/Applications/Koan.app/Contents/Resources/bin/koan', text)

    def test_only_one_skill_in_the_repo(self):
        # `npx skills add` would offer each SKILL.md under skills/.
        found = sorted(p.relative_to(ROOT).as_posix()
                       for p in (ROOT / 'skills').rglob('SKILL.md'))
        self.assertEqual(found, ['skills/koan-browser/SKILL.md'])


if __name__ == '__main__':
    unittest.main()
