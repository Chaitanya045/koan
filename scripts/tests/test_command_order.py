#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Every command-palette command has a place in Zen's order.

MakeCommand() in src/browser/ui/views/koan_commands.cc looks each command's id
up in kZenOrder. A missing id once crashed the URL bar on every keystroke
("open-space-routing", #233). This parses the C++ file, so it runs in CI
without a build.
"""

import pathlib
import re
import unittest

VIEWS = pathlib.Path(__file__).resolve().parents[2] / 'src/browser/ui/views'
SOURCE = VIEWS / 'koan_commands.cc'
# Where the id prefixes MakeCommand() is called with are defined.
PREFIXES = VIEWS / 'koan_command_palette.h'


def ordered_ids(text):
    table = re.search(r'kZenOrder\s*=.*?\(\{(.*?)\}\);', text, re.S)
    assert table, 'kZenOrder not found'
    return set(re.findall(r'\{"([a-z-]+)",\s*\d+\}', table.group(1)))


def command_ids(text):
    # MakeCommand("id", ...) and ShortcutCommand(controller, "id", ...), with
    # the id on the same or the next line.
    found = re.findall(
        r'(?:MakeCommand\(|ShortcutCommand\(\s*[a-z_]+,)\s*"([a-z-]+)"', text)
    # MakeCommand(kSomePrefix + ...): the prefix's id before ':'.
    for name in re.findall(r'MakeCommand\(\s*(k[A-Za-z]+Prefix)\b', text):
        value = re.search(name + r'\[\]\s*=\s*"([a-z-]+):?"',
                          text + PREFIXES.read_text())
        assert value, name + ' not found'
        found.append(value.group(1))
    return set(found)


class CommandOrderTest(unittest.TestCase):

    def test_every_command_is_ordered(self):
        text = SOURCE.read_text()
        ids = command_ids(text)
        self.assertGreater(len(ids), 20)
        self.assertEqual(sorted(ids - ordered_ids(text)), [])


if __name__ == '__main__':
    unittest.main()
