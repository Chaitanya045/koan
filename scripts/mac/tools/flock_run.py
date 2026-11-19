#!/usr/bin/env python3
# Copyright 2026 The Koan Authors
# Use of this source code is governed by the MIT license that can be
# found in the LICENSE file.
"""Runs a command while holding an exclusive flock(2) on a lock file.

  python3 -I scripts/mac/tools/flock_run.py <lock file> -- <command...>

macOS has no flock(1). The Mac loop's builds and e2e runs take
$KOAN_MAC/tmp/e2e.lock with a stand-in of its own; this is the same flock(2)
lock, so the two exclude each other. It waits for the lock (printing that it
does), then execs the command with the lock on file descriptor 9, which the
command and its children inherit: the lock is held until the last of them
exits, even if the script that started them is killed. KOAN_LOCK_WAIT=0
makes it give up at once (exit 75) when the lock is taken.

Works with macOS's /usr/bin/python3 (3.9).
"""

import fcntl
import os
import sys

LOCK_FD = 9
EX_TEMPFAIL = 75


def main(argv):
  if len(argv) < 3 or argv[1] != '--':
    print('usage: flock_run.py <lock file> -- <command...>', file=sys.stderr)
    return 2
  path, cmd = argv[0], argv[2:]
  os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
  fd = os.open(path, os.O_RDWR | os.O_CREAT, 0o644)
  try:
    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
  except BlockingIOError:
    if os.environ.get('KOAN_LOCK_WAIT', '1') == '0':
      print(f'{path} is held by another build or test run', file=sys.stderr)
      return EX_TEMPFAIL
    print(f'waiting for {path}: another build or test run holds it '
          '(Ctrl-C to give up)', file=sys.stderr, flush=True)
    fcntl.flock(fd, fcntl.LOCK_EX)
  if fd != LOCK_FD:
    # The duplicate shares the open file, and so the lock; dup2 makes it
    # inheritable.
    os.dup2(fd, LOCK_FD)
    os.close(fd)
  os.set_inheritable(LOCK_FD, True)
  os.execvp(cmd[0], cmd)
  return 127  # not reached


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
