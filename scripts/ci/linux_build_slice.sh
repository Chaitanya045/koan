#!/usr/bin/env bash
# Linux CI: builds Koan (out/Release) with scripts/build.sh until it is done
# or a deadline passes, whichever comes first. The Linux twin of
# scripts/ci/mac_build_slice.sh, whose header has the details: at the
# deadline siso gets SIGINT and saves its state; anything still running 10
# minutes later is killed.
#
#   KOAN_CI_DEADLINE=<unix time> scripts/ci/linux_build_slice.sh <log dir>
#
# Writes status=finished|running and progress=[done/total] to
# $GITHUB_OUTPUT (when set) and exits 0 for both; a build error exits
# non-zero and prints the failed steps. Build output goes to
# <log dir>/build.log; progress, memory and swap every 5 min to the job log
# and <log dir>/memory.log.
#
# The resume check of mac_build_slice.sh (issue #534) runs here too: a job
# that has far more to do than the previous job left fails after 10 min.
# The compile steps don't depend on the runner image (Chromium's clang and
# sysroot come from the source archive), but anything else that makes siso
# redo finished work would stall the chain the same way.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LOGS="${1:?usage: KOAN_CI_DEADLINE=<unix time> $0 <log dir>}"
DEADLINE="${KOAN_CI_DEADLINE:?set KOAN_CI_DEADLINE}"
# shellcheck source=scripts/ci/linux_env.sh
source "$ROOT/scripts/ci/linux_env.sh"
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

RESUME="$ROOT/chromium/src/out/Release/koan-ci-resume"

# Job control gives the build its own process group, so SIGINT reaches
# build.sh, autoninja and siso together.
set -m
scripts/build.sh chrome >> "$LOGS/build.log" 2>&1 &
build=$!
set +m
timer="$LOGS/.deadline-reached"
rebuilding="$LOGS/.rebuilding"
rm -f "$timer" "$rebuilding"

(
  ticks=0
  checked=0
  while sleep 300; do
    ticks=$((ticks + 1))
    if ((ticks >= 2 && checked == 0)); then
      rc=0
      python3 -I scripts/ci/build_progress.py check "$LOGS/build.log" \
        "$RESUME" || rc=$?
      if ((rc == 1)); then
        touch "$rebuilding"
        echo "resume check failed: stopping the build" >> "$LOGS/build.log"
        kill -INT -- "-$build" 2>/dev/null || true
      fi
      ((rc == 2)) || checked=1
    fi
    line="$(tail -c 100000 "$LOGS/build.log" | grep -Eo '^\[[0-9]+/[0-9]+\]' |
      tail -n 1 || true)"
    mem="$(free -m | awk '/^Mem:/ { m = $7 " MB available" } /^Swap:/ { s = $3 " MB swap used" } END { print m ", " s }')"
    echo "$(date -u +%H:%M:%S) ${line:-[?]} $mem, disk $(df -h / | awk 'NR == 2 { print $4 }') free" |
      tee -a "$LOGS/memory.log"
  done
) &
monitor=$!

(
  sleep "$budget"
  touch "$timer"
  echo "deadline reached: stopping the build" >> "$LOGS/build.log"
  kill -INT -- "-$build" 2>/dev/null || exit 0
  sleep 600
  kill -KILL -- "-$build" 2>/dev/null || true
) > /dev/null 2>&1 &
watcher=$!

status=0
wait "$build" || status=$?
pkill -P "$watcher" 2>/dev/null || true
pkill -P "$monitor" 2>/dev/null || true
kill "$watcher" "$monitor" 2>/dev/null || true
wait 2>/dev/null || true

progress="$(grep -Eo '^\[[0-9]+/[0-9]+\]' "$LOGS/build.log" | tail -n 1 || true)"
output progress "${progress:-unknown}"
cp chromium/src/out/Release/siso_explain "$LOGS/" 2>/dev/null || true

if [[ -f $rebuilding ]]; then
  rm -f "$rebuilding" "$timer"
  echo "siso's first reasons (siso_explain):" >&2
  head -n 20 "$LOGS/siso_explain" >&2 || true
  exit 1
elif [[ -f $timer ]]; then
  rm -f "$timer"
  echo "stopped at the deadline (siso exit $status); the next job resumes"
  python3 -I scripts/ci/build_progress.py record "$LOGS/build.log" "$RESUME"
  output status running
elif ((status == 0)); then
  output status finished
else
  echo "build failed (exit $status):" >&2
  grep -A 40 '^FAILED:' "$LOGS/build.log" >&2 || tail -n 60 "$LOGS/build.log" >&2
  exit "$status"
fi
