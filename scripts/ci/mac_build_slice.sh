#!/usr/bin/env bash
# macOS CI: builds Koan.app with scripts/build.sh until it is done or a
# deadline passes, whichever comes first.
#
#   KOAN_CI_DEADLINE=<unix time> scripts/ci/mac_build_slice.sh <log dir>
#
# GitHub ends a job after 6 h and a first Chromium build on a 3-core runner
# takes far longer, so .github/workflows/macos-build.yml chains jobs: each
# runs this, packs out/Koan when it stops, and the next job unpacks it and
# carries on. At the deadline siso gets SIGINT, which stops it cleanly with
# its state saved; anything still unfinished after 10 more minutes is
# killed (a step that was cut short reruns next time).
#
# Writes status=finished|running to $GITHUB_OUTPUT (when set) and exits 0
# for both; a build error exits non-zero. Logs the build and the memory
# use (every 5 min) to <log dir>.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LOGS="${1:?usage: KOAN_CI_DEADLINE=<unix time> $0 <log dir>}"
DEADLINE="${KOAN_CI_DEADLINE:?set KOAN_CI_DEADLINE}"
# shellcheck source=scripts/ci/mac_env.sh
source "$ROOT/scripts/ci/mac_env.sh"
cd "$ROOT"
mkdir -p "$LOGS"

output() { echo "$1=$2"; [[ -z ${GITHUB_OUTPUT:-} ]] || echo "$1=$2" >> "$GITHUB_OUTPUT"; }

budget=$((DEADLINE - $(date +%s)))
if ((budget < 600)); then
  echo "less than 10 minutes left; not starting the build"
  output status running
  exit 0
fi
echo "build budget: $((budget / 60)) min, JOBS=${JOBS:-default}"

# Memory and swap over time, to tune JOBS on the 7 GB runners.
(
  while sleep 300; do
    echo "== $(date -u +%H:%M:%S) $(sysctl -n vm.swapusage)"
    memory_pressure -Q 2>/dev/null || true
  done
) > "$LOGS/memory.log" 2>&1 &
monitor=$!

# Job control gives the build its own process group, so SIGINT reaches
# build.sh, autoninja and siso together (and isn't ignored, as it is for
# background jobs without job control).
set -m
scripts/build.sh chrome >> "$LOGS/build.log" 2>&1 &
build=$!
set +m
timer="$LOGS/.deadline-reached"
rm -f "$timer"
(
  sleep "$budget"
  touch "$timer"
  echo "deadline reached: stopping the build" >> "$LOGS/build.log"
  kill -INT -- "-$build" 2>/dev/null || exit 0
  sleep 600
  kill -KILL -- "-$build" 2>/dev/null || true
) > /dev/null 2>&1 &
watcher=$!
tail -f "$LOGS/build.log" &
tailer=$!

status=0
wait "$build" || status=$?
# The helpers' sleeps first, so nothing outlives the step.
pkill -P "$watcher" 2>/dev/null || true
pkill -P "$monitor" 2>/dev/null || true
kill "$watcher" "$monitor" "$tailer" 2>/dev/null || true
wait 2>/dev/null || true

# The last progress line ([done/total]), for the job summary.
progress="$(grep -Eo '^\[[0-9]+/[0-9]+\]' "$LOGS/build.log" | tail -n 1 || true)"
output progress "${progress:-unknown}"

if [[ -f $timer ]]; then
  rm -f "$timer"
  echo "stopped at the deadline (siso exit $status); the next job resumes"
  output status running
elif ((status == 0)); then
  output status finished
else
  echo "build failed (exit $status); last lines:" >&2
  tail -n 60 "$LOGS/build.log" >&2
  exit "$status"
fi
