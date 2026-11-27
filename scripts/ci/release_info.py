#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""The release workflow's checks and names (.github/workflows/release.yml).

  release_info.py check --tag v0.0.2-alpha.1
      Run in a checkout of the tag, with its tags fetched. Fails, before
      hours of building, unless:
        - the tag is v<VERSION>;
        - docs/releases/<VERSION>.md exists and has no TODO(release) left;
        - the app's CFBundleVersion goes up from the previous release's
          (the highest v* tag below this one).
      Prints version, short, label, title, chromium, bundle, notes,
      previous_tag and latest (true unless a v* tag has a higher version,
      e.g. a fix on a release/<major> branch) as key=value lines, also to
      $GITHUB_OUTPUT.

  release_info.py notes --notes docs/releases/<v>.md --dmg <file>
      --sha256 <hex> --flatpak <file> --flatpak-sha256 <hex>
      [--generated <file>] --output <file>
      The release's body: the notes with @DMG@, @SHA256@, @FLATPAK@ and
      @FLATPAK_SHA256@ filled in, then GitHub's generated notes (the pull
      requests since the previous tag). Notes that don't mention @FLATPAK@
      get a short Linux section after them, so the release always names the
      Flatpak and its checksum.

The version rules are in src/version/koan_version.py, which the build uses
too, so the title, Info.plist and About Koan can't disagree.
"""

import argparse
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src' / 'version'))
import koan_version  # noqa: E402

TODO = 'TODO(release)'
_TAG = re.compile(r'^v(.+)$')


class ReleaseError(Exception):
  pass


def version_from_tag(tag):
  match = _TAG.match(tag)
  if not match:
    raise ReleaseError('tag %r does not start with v' % tag)
  try:
    return koan_version.parse(match.group(1))
  except ValueError as e:
    raise ReleaseError('tag %r: %s' % (tag, e))


def check_tag(tag, version):
  if version_from_tag(tag).text != version.text:
    raise ReleaseError('tag %s does not match VERSION %s: bump VERSION or '
                       'tag v%s' % (tag, version.text, version.text))


def notes_path(root, version):
  return pathlib.Path(root) / 'docs' / 'releases' / (version.text + '.md')


def check_notes(path):
  if not path.is_file():
    raise ReleaseError('no release notes: write %s (docs/RELEASING.md)' % path)
  text = path.read_text()
  if not text.strip():
    raise ReleaseError('%s is empty' % path)
  left = [n for n, line in enumerate(text.splitlines(), 1) if TODO in line]
  if left:
    raise ReleaseError('%s still has %s on line(s) %s' %
                       (path, TODO, ', '.join(map(str, left))))


def previous_tag(tags, version):
  """The highest v<SemVer> tag below `version`, or ''. Others are ignored."""
  best, best_key = '', None
  current = koan_version.counter(version)
  for tag in tags:
    try:
      other = version_from_tag(tag.strip())
    except ReleaseError:
      continue
    key = koan_version.counter(other)
    if key < current and (best_key is None or key > best_key):
      best, best_key = tag.strip(), key
  return best


def is_latest(tags, version):
  """Whether no v<SemVer> tag is above `version` (others are ignored)."""
  current = koan_version.counter(version)
  for tag in tags:
    try:
      other = version_from_tag(tag.strip())
    except ReleaseError:
      continue
    if koan_version.counter(other) > current:
      return False
  return True


def bundle_at(version_text, pin_text):
  """The CFBundleVersion a release had, from its VERSION and CHROMIUM_VERSION.

  Before VERSION existed (0.0.1-alpha.1), the app had Chromium's own
  CFBundleVersion, BUILD.PATCH.
  """
  chromium = koan_version.parse_chromium(pin_text.splitlines()[0])
  if version_text is None:
    return '%d.%d' % (chromium.build, chromium.patch)
  return koan_version.bundle_version(koan_version.parse(version_text),
                                     chromium)


def check_bundle_goes_up(bundle, previous_bundle, previous):
  if (koan_version.bundle_key(bundle) <=
      koan_version.bundle_key(previous_bundle)):
    raise ReleaseError(
        'CFBundleVersion %s is not above %s, the previous release %s\'s: a '
        'release must not move to an older Chromium than the one before it '
        '(docs/RELEASING.md, "Versions")' % (bundle, previous_bundle,
                                              previous))


def title(version, chromium):
  return koan_version.title(version, chromium)


def info(version, chromium, notes, previous, latest=True):
  return {
      'version': version.text,
      'short': koan_version.short(version),
      'label': koan_version.label(version),
      'title': title(version, chromium),
      'chromium': koan_version.chromium_text(chromium),
      'bundle': koan_version.bundle_version(version, chromium),
      'notes': notes,
      'previous_tag': previous,
      'latest': 'true' if latest else 'false',
  }


LINUX_SECTION = '''## Linux

**@FLATPAK@**: Linux on x86_64, as a Flatpak. Install it for your user and
start it ([Installing the Linux build](https://github.com/koan-browser/koan/blob/develop/docs/USAGE.md#installing-the-linux-build)):

```sh
flatpak install --user ./@FLATPAK@
flatpak run dev.koan.Koan
```

Its SHA-256 checksum is in the `.sha256` file next to it:

```
@FLATPAK_SHA256@  @FLATPAK@
```
'''


def _check_sha256(value):
  if not re.fullmatch(r'[0-9a-f]{64}', value):
    raise ReleaseError('%r is not a SHA-256' % value)


def render_notes(notes, dmg, sha256, generated='', flatpak='',
                 flatpak_sha256=''):
  _check_sha256(sha256)
  body = notes.replace('@DMG@', dmg).replace('@SHA256@', sha256).rstrip()
  if flatpak or flatpak_sha256 or '@FLATPAK' in notes:
    if not flatpak:
      raise ReleaseError('the notes mention the Flatpak but no --flatpak')
    _check_sha256(flatpak_sha256)
    if '@FLATPAK@' not in notes:
      body += '\n\n' + LINUX_SECTION.rstrip()
    body = (body.replace('@FLATPAK_SHA256@', flatpak_sha256)
            .replace('@FLATPAK@', flatpak))
  if generated.strip():
    body += '\n\n---\n\n' + generated.strip()
  return body + '\n'


def _git(root, *args, check=True):
  result = subprocess.run(['git', '-C', str(root), *args],
                          capture_output=True, text=True)
  if check and result.returncode:
    raise ReleaseError('git %s: %s' % (' '.join(args), result.stderr.strip()))
  return result.stdout if result.returncode == 0 else None


def _check(args):
  root = pathlib.Path(args.root)
  version = koan_version.read(root / 'VERSION')
  check_tag(args.tag, version)
  chromium = koan_version.read_pin(root / 'CHROMIUM_VERSION')
  path = notes_path(root, version)
  check_notes(path)
  tags = _git(root, 'tag', '--list', 'v*').split()
  previous = previous_tag(tags, version)
  if previous:
    previous_bundle = bundle_at(
        _git(root, 'show', previous + ':VERSION', check=False),
        _git(root, 'show', previous + ':CHROMIUM_VERSION'))
    check_bundle_goes_up(koan_version.bundle_version(version, chromium),
                         previous_bundle, previous)
  return info(version, chromium, str(path.relative_to(root)), previous,
              is_latest(tags, version))


def main(argv):
  parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
  sub = parser.add_subparsers(dest='command', required=True)
  p = sub.add_parser('check')
  p.add_argument('--tag', required=True)
  p.add_argument('--root', default=str(ROOT))
  p = sub.add_parser('notes')
  p.add_argument('--notes', required=True)
  p.add_argument('--dmg', required=True)
  p.add_argument('--sha256', required=True)
  p.add_argument('--flatpak', default='')
  p.add_argument('--flatpak-sha256', default='')
  p.add_argument('--generated')
  p.add_argument('--output', required=True)
  args = parser.parse_args(argv)

  try:
    if args.command == 'check':
      values = _check(args)
      lines = ['%s=%s' % item for item in values.items()]
      print('\n'.join(lines))
      if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a') as f:
          f.write('\n'.join(lines) + '\n')
    else:
      generated = ''
      if args.generated:
        generated = pathlib.Path(args.generated).read_text()
      body = render_notes(pathlib.Path(args.notes).read_text(), args.dmg,
                          args.sha256, generated, args.flatpak,
                          args.flatpak_sha256)
      pathlib.Path(args.output).write_text(body)
  except (ReleaseError, ValueError, OSError) as e:
    print('release_info.py: %s' % e, file=sys.stderr)
    return 1
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
