#!/bin/bash
# quit.sh <pid>: quits one Koan that launch.sh started, by its exact pid, and
# only if its command line has a --user-data-dir under $KOAN_MAC_ROOT/rec-
# (never the user's Koan, whatever pid it is given). SIGTERM, then SIGKILL
# after 15 s.
set -euo pipefail
M="${KOAN_MAC_ROOT:-$HOME/work/koan-mac}"
pid="${1:?usage: quit.sh <pid>}"
[[ "$pid" =~ ^[0-9]+$ ]] || { echo "quit.sh: not a pid: $pid" >&2; exit 2; }
cmd="$(ps -o command= -p "$pid" 2>/dev/null || true)"
[[ -n "$cmd" ]] || { echo "quit.sh: $pid is not running"; exit 0; }
case "$cmd" in
  *"--user-data-dir=$M/rec-"*) ;;
  *) echo "quit.sh: refusing: pid $pid is not a launch.sh Koan ($(cut -c1-120 <<<"$cmd"))" >&2; exit 3 ;;
esac
kill -TERM "$pid"
for _ in $(seq 1 30); do
  kill -0 "$pid" 2>/dev/null || { echo "quit.sh: $pid quit"; exit 0; }
  sleep 0.5
done
kill -KILL "$pid" 2>/dev/null || true
echo "quit.sh: $pid killed after 15 s"
