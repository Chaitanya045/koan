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
# for both; a build error exits non-zero and prints the failed steps. The
# whole build output goes to <log dir>/build.log; progress, memory and swap
# are printed every 5 min (and kept in <log dir>/memory.log).
#
# Resuming (issue #534): the runner image, Xcode and the SDK siso sees are
# printed at the start and kept in <log dir>/toolchain.txt, and the SDK's
# files get one fixed mtime, so that a job on a runner image of another
# date doesn't redo the earlier jobs' work (details below). A job that
# stops at the deadline records how many steps it left
# (scripts/ci/build_progress.py); 10 minutes into the next job, if siso has
# far more than that to do, it is rebuilding an earlier job's work, and the
# job stops and fails instead of burning the rest of the chain. siso's own
# reasons are copied to <log dir>/siso_explain.
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

OUT="$ROOT/chromium/src/out/Koan"
RESUME="$OUT/koan-ci-resume"

# The runner image and the SDK the build depends on: every object's deps
# start with the SDK's SDKSettings.json, and siso's state records the
# SDK's usr/include (see below). So a rebuild can be traced to the runner.
toolchain() {
  echo "image: ${ImageOS:-?} ${ImageVersion:-?}"
  xcodebuild -version | paste -sd' ' -
  local sdk l
  sdk="$(xcrun --show-sdk-path)"
  echo "SDK: $sdk (real path $(cd "$sdk" && pwd -P))"
  stat -f '%N mtime %Fm (%Sm)' "$sdk/SDKSettings.json" \
    "$sdk/usr/include/mach/exc.defs"
  for l in "$OUT"/sdk/xcode_links/*; do
    [[ -L $l ]] && stat -f '%N -> %Y, link mtime %Fm' "$l"
  done
}
echo "==> Runner and SDK"
{ toolchain 2>&1 || true; } | tee "$LOGS/toolchain.txt"

# One mtime for the whole SDK, the same on every runner image (issue
# #534). //build/modules:mac_sysroot and //build/config/mac:sdk_inputs are
# noop.py actions that declare the SDK's usr/include as their outputs, so
# siso's state records those headers with their mtime, which is when the
# runner image was made. On an image of another date the mtimes differ,
# siso forgets when the action ran, finds its "outputs" older than
# noop.py and runs it again; that changes gen/build/modules/module.modulemap,
# which rebuilds the system module (obj/build/modules/system/module.pcm)
# and every object that imports it: most of the build.
# Only with the Xcode and SDK the fetch job recorded (mac_fetch.sh writes
# .cache/ci-toolchain the same way): same Xcode build, same headers. With
# another one, the mtimes stay as they are, so siso rebuilds what the new
# SDK changes, and the resume check is off.
resume_check=1
toolchain_now="$(xcodebuild -version | paste -sd' ' -), SDK $(xcrun --show-sdk-version)"
if [[ $toolchain_now == "$(cat .cache/ci-toolchain 2>/dev/null)" ]]; then
  sdks="$(dirname "$(cd "$(xcrun --show-sdk-path)" && pwd -P)")"
  sudo find "$sdks" -exec touch -h -d 2001-01-01T00:00:00Z {} +
  echo "SDK mtimes set to 2001-01-01 (all of $sdks)" | tee -a "$LOGS/toolchain.txt"
else
  echo "::warning::This runner has $toolchain_now, the fetch job had" \
    "$(cat .cache/ci-toolchain 2>/dev/null || echo '?'): siso rebuilds what" \
    "the SDK change touches, and the resume check is off."
  resume_check=0
fi

# Job control gives the build its own process group, so SIGINT reaches
# build.sh, autoninja and siso together (and isn't ignored, as it is for
# background jobs without job control).
set -m
scripts/build.sh chrome >> "$LOGS/build.log" 2>&1 &
build=$!
set +m
timer="$LOGS/.deadline-reached"
rebuilding="$LOGS/.rebuilding"
rm -f "$timer" "$rebuilding"

# Progress, memory and swap every 5 minutes: siso's own output (every step
# it starts and ends) is too much for the job log, so it goes to build.log.
# From 10 minutes on, until it has an answer, the resume check.
(
  ticks=0
  checked=0
  while sleep 300; do
    ticks=$((ticks + 1))
    if ((resume_check && ticks >= 2 && checked == 0)); then
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
    swap="$(sysctl -n vm.swapusage | sed 's/  */ /g')"
    free="$(memory_pressure -Q 2>/dev/null | sed -n 's/.*percentage: //p')"
    echo "$(date -u +%H:%M:%S) ${line:-[?]} memory free ${free:-?}, swap$swap" |
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
# The helpers' sleeps first, so nothing outlives the step.
pkill -P "$watcher" 2>/dev/null || true
pkill -P "$monitor" 2>/dev/null || true
kill "$watcher" "$monitor" 2>/dev/null || true
wait 2>/dev/null || true

# Why siso ran each step it ran (it always writes this), and the SDK as gn
# gen left it.
cp "$OUT/siso_explain" "$LOGS/" 2>/dev/null || true
{ echo "==> After the build"; toolchain 2>&1 || true; } >> "$LOGS/toolchain.txt"

# The last progress line ([done/total]), for the job summary.
progress="$(grep -Eo '^\[[0-9]+/[0-9]+\]' "$LOGS/build.log" | tail -n 1 || true)"
output progress "${progress:-unknown}"

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
