#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Lists the per-profile KeyedService factories of a Chromium checkout.

    scripts/agent/list_keyed_services.py [--chromium-src DIR] [--write]

An agent container is a storage partition inside the user's profile, so it
shares every per-profile KeyedService (.agent/notes/agent-control.md section
7.2). Each must be decided: agent-safe, or blocked for agent containers. The
decisions are src/browser/containers/koan_agent_service_policy.cc; this script
finds the factories so that a Chromium roll that adds one fails
scripts/agent/check_agent_services.py until it is decided.

It reads the checkout's HEAD (`git grep`, so local patches don't count) and
finds the name every factory gives its base class, which is what
KeyedServiceBaseFactory::name() returns at runtime:

  * ProfileKeyedServiceFactory("Name", ...), BrowserContextKeyedServiceFactory,
    and their Refcounted variants;
  * BrowserContextKeyedAPIFactory<T> (extensions), named by T::service_name().

Test-only code and platforms Koan doesn't ship (Android, ChromeOS, iOS,
Fuchsia) are skipped. A factory constructed with a name that is not a string
literal can't be read; those files are listed under "unnamed" and the runtime
test (koan_agent_service_policy_unittest.cc) covers them, as it covers anything
this static scan gets wrong.

--write updates scripts/agent/keyed_services.json, stamped with the checkout's
HEAD commit; scripts/update_chromium.sh does it on a roll. Without --write it
prints the inventory.
"""

import argparse
import json
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
INVENTORY = pathlib.Path(__file__).resolve().parent / 'keyed_services.json'

ROOTS = ('chrome', 'components', 'extensions')
_SKIP = re.compile(
    r'(^|/)(test|tests|android|ash|chromeos|ios|fuchsia|lacros)(/|$)|'
    r'(_test|_unittest|_browsertest|_interactive_uitest|_fuzzer|test_util|'
    r'_test_util|_test_utils|_test_helper|fake_|mock_|_fake|_mock|testing_)'
    r'[a-z_]*\.cc$')
_FACTORY = re.compile(
    r'\b(?:Refcounted)?(?:Profile|BrowserContext)KeyedServiceFactory\(\s*'
    r'(?:"((?:[^"\\]|\\.)*)"|([A-Za-z_][\w:]*))')
_API_NAME = re.compile(
    r'static\s+(?:constexpr\s+)?const\s+char\s*\*\s*service_name\(\)\s*'
    r'(?:const\s*)?\{\s*return\s+"([^"]+)"\s*;')


def head_files(src):
  out = subprocess.run(
      ['git', '-C', str(src), 'grep', '-l', '-E',
       r'KeyedServiceFactory\(|service_name\(\)', 'HEAD', '--'] +
      ['%s/*.cc' % r for r in ROOTS] + ['%s/*.h' % r for r in ROOTS],
      capture_output=True, text=True, check=True).stdout
  for line in out.splitlines():
    path = line.split(':', 1)[1]
    if not _SKIP.search(path):
      yield path


def read(src, path):
  return subprocess.run(['git', '-C', str(src), 'show', 'HEAD:' + path],
                        capture_output=True, text=True, check=True).stdout


def scan(src):
  factories = {}
  unnamed = set()
  for path in head_files(src):
    text = read(src, path)
    for m in _FACTORY.finditer(text):
      if m.group(1) is not None:
        factories.setdefault(m.group(1), path)
      else:
        # A constant of the file, or a parameter (a base class's own
        # constructor, or a factory named by its caller).
        ident = m.group(2).split('::')[-1]
        const = re.search(
            r'char\s+%s\[\]\s*=\s*"([^"]+)"' % re.escape(ident), text)
        if const:
          factories.setdefault(const.group(1), path)
        else:
          unnamed.add(path)
    for m in _API_NAME.finditer(text):
      factories.setdefault(m.group(1), path)
  return factories, sorted(unnamed)


def head_commit(src):
  return subprocess.run(['git', '-C', str(src), 'rev-parse', 'HEAD'],
                        capture_output=True, text=True,
                        check=True).stdout.strip()


def main():
  parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
  parser.add_argument('--chromium-src',
                      default=os.environ.get('KOAN_CHROMIUM_SRC',
                                             str(ROOT / 'chromium/src')))
  parser.add_argument('--write', action='store_true')
  args = parser.parse_args()
  src = pathlib.Path(args.chromium_src)
  factories, unnamed = scan(src)
  inventory = {
      'chromium_commit': head_commit(src),
      'factories': {name: factories[name] for name in sorted(factories)},
      'unnamed': unnamed,
  }
  text = json.dumps(inventory, indent=1, sort_keys=False) + '\n'
  if args.write:
    INVENTORY.write_text(text)
    print('%d factories, %d files with unnamed ones -> %s' %
          (len(factories), len(unnamed), INVENTORY.relative_to(ROOT)))
  else:
    sys.stdout.write(text)
  return 0


if __name__ == '__main__':
  sys.exit(main())
