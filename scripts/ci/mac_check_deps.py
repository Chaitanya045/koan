#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Checks that gclient checked out every git dependency, and fills gaps.

    mac_check_deps.py <chromium dir> <chromium version>

Run in <chromium dir> (the one with .gclient) after `gclient sync`, with
depot_tools on PATH. Used by scripts/ci/mac_fetch.sh.

gclient can report success with a dependency missing: after a fetch
fails, it moves the half-made directory to _bad_scm/, and a second sync
may not fetch it again. And googlesource sometimes loses commits that
Chromium pins: since 2026-11-03 it answers fetches of
external/search_engines_data at 5dc9f1af with HTTP 500 and gitiles has
no such commit, although Chromium main and the 158 tags still pin it.

So: every `src/...: <git url>@<commit>` line of `gclient revinfo` must be
a directory with more than a .git in it. Missing ones are taken from
Chromium's official source tarball for <chromium version>
(chromium-<version>-lite.tar.xz on commondatastorage, which has every
dependency checked out), but only if that version's DEPS pins the same
commit. Exits non-zero if anything is still missing.

For tests: KOAN_REVINFO=<file> reads the revinfo output from a file, and
KOAN_TARBALL=<url> takes another tarball (e.g. file://...).
Works with Python 3.9.
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request

REVINFO_LINE = re.compile(r'^(src(?:/[^:\s]+)?): (https?://\S+?)@([0-9a-f]{40})$')
TARBALL = ('https://commondatastorage.googleapis.com/chromium-browser-official/'
           'chromium-{0}-lite.tar.xz')
TAG_DEPS = ('https://chromium.googlesource.com/chromium/src/+/refs/tags/{0}/'
            'DEPS?format=TEXT')


def git_deps():
    if os.environ.get('KOAN_REVINFO'):
        with open(os.environ['KOAN_REVINFO']) as f:
            text = f.read()
    else:
        text = subprocess.run(['gclient', 'revinfo'], check=True,
                              stdout=subprocess.PIPE,
                              universal_newlines=True).stdout
    deps = []
    for line in text.splitlines():
        m = REVINFO_LINE.match(line.strip())
        if m and m.group(1) != 'src':
            deps.append((m.group(1), m.group(2), m.group(3)))
    return deps


def missing(deps):
    gone = []
    for path, url, commit in deps:
        if not os.path.isdir(path) or not [
                e for e in os.listdir(path) if e != '.git']:
            gone.append((path, url, commit))
    return gone


def fill_from_tarball(gone, version):
    import base64
    with urllib.request.urlopen(TAG_DEPS.format(version), timeout=60) as r:
        tag_deps = base64.b64decode(r.read()).decode()
    usable = []
    for path, url, commit in gone:
        if commit in tag_deps:
            usable.append(path)
        else:
            print('not in %s\'s DEPS at %s: %s' % (version, commit[:8], path))
    if not usable:
        return
    top = 'chromium-%s' % version
    members = [top + path[len('src'):] for path in usable]
    url = os.environ.get('KOAN_TARBALL') or TARBALL.format(version)
    print('taking from %s: %s' % (url, ', '.join(usable)))
    sys.stdout.flush()
    tmp = tempfile.mkdtemp(dir='.')
    try:
        # Streams the ~2 GB tarball once and extracts only these paths.
        curl = subprocess.Popen(
            ['curl', '-fsSL', '--retry', '5', url],
            stdout=subprocess.PIPE)
        tar = subprocess.run(['tar', '-xJf', '-', '-C', tmp] + members,
                             stdin=curl.stdout)
        curl.stdout.close()
        if curl.wait() != 0 or tar.returncode != 0:
            print('could not extract them from the tarball')
            return
        for path, member in zip(usable, members):
            if os.path.lexists(path):
                shutil.rmtree(path)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            os.rename(os.path.join(tmp, member), path)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv):
    if len(argv) != 3 or not os.path.isfile(os.path.join(argv[1], '.gclient')):
        sys.stderr.write(
            'usage: mac_check_deps.py <chromium dir> <chromium version>\n')
        return 2
    os.chdir(argv[1])
    deps = git_deps()
    if len(deps) < 20:
        print('gclient revinfo listed only %d git dependencies' % len(deps))
        return 1
    gone = missing(deps)
    print('%d git dependencies, %d missing' % (len(deps), len(gone)))
    if gone:
        for path, url, commit in gone:
            print('missing: %s (%s@%s)' % (path, url, commit[:8]))
        fill_from_tarball(gone, argv[2])
        gone = missing(deps)
        for path, url, commit in gone:
            print('still missing: %s' % path)
    return 1 if gone else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
