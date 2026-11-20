#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Koan's version: the one place that turns VERSION into what ships.

VERSION, at the root of the Koan repository, holds Koan's SemVer version:
MAJOR.MINOR.PATCH, optionally with a pre-release label -alpha.N, -beta.N or
-rc.N (e.g. 0.0.2-alpha.1). Chromium's version (chrome/VERSION,
CHROMIUM_VERSION) stays what the web sees; Koan's is shown next to it.

From it this module derives (docs/RELEASING.md, "Versions"):
  short      MAJOR.MINOR.PATCH, the Mac app's CFBundleShortVersionString
             (Apple allows only three integers there)
  label      '', 'Alpha', 'Beta' or 'RC'; 'Alpha 2' etc. from .2 on, so two
             pre-releases of the same version never look the same
  counter    one integer that grows with the SemVer order:
             ((MAJOR * 100 + MINOR) * 100 + PATCH) * 1000 + stage * 100 + N,
             stage 1 alpha, 2 beta, 3 rc, and 900 for a final release
  bundle     CFBundleVersion = <Chromium BUILD>.<Chromium PATCH>.<counter>,
             e.g. 8127.18.2101: it only goes up (for a Sparkle updater) as
             long as a Koan release never moves to an older Chromium, which
             scripts/ci/release_info.py checks against the previous release,
             and it stays above 0.0.1's 8127.18 (Chromium's own value), so
             LaunchServices prefers a newer copy of Koan.app to an older one
  title      'Koan 0.0.2 (Alpha) · Chromium 158.0.8127.18'

Used by the build (//koan/version: the C++ header and the app's Info.plist),
by scripts/ci/mac_package.sh and by the release workflow
(scripts/ci/release_info.py). Keep it Python 3.9 compatible: that is macOS's
/usr/bin/python3.
"""

import argparse
import collections
import os
import pathlib
import plistlib
import re
import sys

# src/version/koan_version.py -> the repository root. realpath, since the
# build runs it through chromium/src/koan, a link to src/.
REPO = pathlib.Path(os.path.realpath(__file__)).parents[2]
VERSION_FILE = REPO / 'VERSION'

_STAGES = {'alpha': 1, 'beta': 2, 'rc': 3}
_FINAL = 9
_LABELS = {'alpha': 'Alpha', 'beta': 'Beta', 'rc': 'RC'}
# The counter keeps MINOR, PATCH and N below 100 (two digits each).
_LIMIT = 99

_SEMVER = re.compile(r'^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)'
                     r'(?:-(alpha|beta|rc)\.([1-9][0-9]*))?$')
_CHROMIUM = re.compile(r'^([0-9]+)\.([0-9]+)\.([0-9]+)\.([0-9]+)$')

Version = collections.namedtuple('Version',
                                 'major minor patch stage number text')
ChromiumVersion = collections.namedtuple('ChromiumVersion',
                                         'major minor build patch')


def parse(text):
  """A Version from 'X.Y.Z' or 'X.Y.Z-alpha.N'; ValueError if it isn't."""
  text = text.strip()
  match = _SEMVER.match(text)
  if not match:
    raise ValueError(
        '%r is not a Koan version: MAJOR.MINOR.PATCH, optionally with '
        '-alpha.N, -beta.N or -rc.N (e.g. 0.0.2-alpha.1)' % text)
  major, minor, patch = (int(match.group(i)) for i in (1, 2, 3))
  stage = match.group(4) or ''
  number = int(match.group(5) or 0)
  for name, value in (('MINOR', minor), ('PATCH', patch), ('N', number)):
    if value > _LIMIT:
      raise ValueError('%r: %s above %d does not fit CFBundleVersion' %
                       (text, name, _LIMIT))
  return Version(major, minor, patch, stage, number, text)


def read(path=VERSION_FILE):
  with open(path) as f:
    return parse(f.read())


def short(version):
  return '%d.%d.%d' % (version.major, version.minor, version.patch)


def label(version):
  if not version.stage:
    return ''
  name = _LABELS[version.stage]
  return name if version.number == 1 else '%s %d' % (name, version.number)


def counter(version):
  base = (version.major * 100 + version.minor) * 100 + version.patch
  if version.stage:
    return base * 1000 + _STAGES[version.stage] * 100 + version.number
  return base * 1000 + _FINAL * 100


def parse_chromium(text):
  """A ChromiumVersion from '158.0.8127.18'."""
  match = _CHROMIUM.match(text.strip())
  if not match:
    raise ValueError('%r is not a Chromium version' % text)
  return ChromiumVersion(*(int(g) for g in match.groups()))


def read_chromium_version_file(path):
  """chrome/VERSION (MAJOR=158 ...) as a ChromiumVersion."""
  values = {}
  with open(path) as f:
    for line in f:
      key, _, value = line.strip().partition('=')
      values[key] = value
  return parse_chromium('.'.join(
      values.get(key, '') for key in ('MAJOR', 'MINOR', 'BUILD', 'PATCH')))


def read_pin(path):
  """CHROMIUM_VERSION (line 1: the version) as a ChromiumVersion."""
  with open(path) as f:
    return parse_chromium(f.readline())


def chromium_text(chromium):
  return '%d.%d.%d.%d' % tuple(chromium)


def bundle_version(version, chromium):
  """CFBundleVersion: '<Chromium BUILD>.<Chromium PATCH>.<counter>'."""
  return '%d.%d.%d' % (chromium.build, chromium.patch, counter(version))


def bundle_key(bundle):
  """A CFBundleVersion as a tuple that sorts as Sparkle and macOS do."""
  return tuple(int(part) for part in bundle.split('.'))


def title(version, chromium):
  """The release's name, e.g. 'Koan 0.0.2 (Alpha) · Chromium 158.0.8127.18'."""
  name = 'Koan ' + short(version)
  if label(version):
    name += ' (%s)' % label(version)
  return '%s · Chromium %s' % (name, chromium_text(chromium))


def _c_string(value):
  return '"%s"' % value.replace('\\', '\\\\').replace('"', '\\"')


def header(version):
  return ('// Generated by //koan/version/koan_version.py from VERSION. Do '
          'not edit.\n'
          '#ifndef KOAN_VERSION_KOAN_VERSION_VALUES_H_\n'
          '#define KOAN_VERSION_KOAN_VERSION_VALUES_H_\n\n'
          '#define KOAN_VERSION_FULL %s\n'
          '#define KOAN_VERSION_SHORT %s\n'
          '#define KOAN_VERSION_LABEL %s\n\n'
          '#endif  // KOAN_VERSION_KOAN_VERSION_VALUES_H_\n' %
          (_c_string(version.text), _c_string(short(version)),
           _c_string(label(version))))


def tweak_plist(plist, version, chromium):
  """Koan's version in the app's Info.plist (the outer bundle only)."""
  plist['CFBundleShortVersionString'] = short(version)
  plist['CFBundleVersion'] = bundle_version(version, chromium)
  # The whole SemVer version, which CFBundleShortVersionString can't hold.
  plist['KoanVersion'] = version.text
  return plist


def _write_depfile(path, output, deps):
  with open(path, 'w') as f:
    f.write('%s: %s\n' % (output, ' '.join(
        str(d).replace(' ', '\\ ') for d in deps)))


def main(argv):
  parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
  parser.add_argument('--version-file', default=str(VERSION_FILE))
  sub = parser.add_subparsers(dest='command', required=True)
  p = sub.add_parser('header', help='write koan_version_values.h')
  p.add_argument('--output', required=True)
  p.add_argument('--depfile', required=True)
  p = sub.add_parser('plist', help="put Koan's version into an Info.plist")
  p.add_argument('--input', required=True)
  p.add_argument('--output', required=True)
  p.add_argument('--depfile', required=True)
  p.add_argument('--chromium-version-file', required=True,
                 help='chrome/VERSION')
  p = sub.add_parser('show', help='print one derived value')
  p.add_argument('what', choices=('full', 'short', 'label', 'counter',
                                  'bundle', 'title'))
  p.add_argument('--chromium', help="Chromium's version, for bundle and title")
  args = parser.parse_args(argv)

  try:
    version = read(args.version_file)
  except (OSError, ValueError) as e:
    print('koan_version.py: %s' % e, file=sys.stderr)
    return 1

  if args.command == 'header':
    text = header(version)
    # Only rewrite a changed header, so a no-op run recompiles nothing.
    try:
      with open(args.output) as f:
        same = f.read() == text
    except OSError:
      same = False
    if not same:
      with open(args.output, 'w') as f:
        f.write(text)
    _write_depfile(args.depfile, args.output, [args.version_file])
  elif args.command == 'plist':
    chromium = read_chromium_version_file(args.chromium_version_file)
    with open(args.input, 'rb') as f:
      plist = plistlib.load(f)
    tweak_plist(plist, version, chromium)
    with open(args.output, 'wb') as f:
      plistlib.dump(plist, f, fmt=plistlib.FMT_XML)
    _write_depfile(args.depfile, args.output, [args.version_file])
  else:
    chromium = parse_chromium(args.chromium) if args.chromium else None
    if args.what in ('bundle', 'title') and not chromium:
      print('koan_version.py: show %s needs --chromium' % args.what,
            file=sys.stderr)
      return 2
    value = {
        'full': lambda: version.text,
        'short': lambda: short(version),
        'label': lambda: label(version),
        'counter': lambda: str(counter(version)),
        'bundle': lambda: bundle_version(version, chromium),
        'title': lambda: title(version, chromium),
    }[args.what]()
    print(value)
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
