#!/usr/bin/env bash
# Runs an end-to-end test against a real Koan build, off-screen.
#
#   scripts/e2e/run.sh <test> [url...]     e.g. scripts/e2e/run.sh shortcuts
#
# Starts Koan (out/Koan, or KOAN_OUT=out/Release) on the headless Ozone
# platform with a fresh profile, UI DevTools and the DevTools protocol, runs
# scripts/e2e/<test>.mjs, and cleans up. Nothing appears on the desktop.
set -euo pipefail

source "$(dirname "$0")/../koan_test_env.sh"
test_name=${1:?usage: run.sh <test> [url...]}
shift
urls=("$@")
[[ ${#urls[@]} -gt 0 ]] || urls=(https://example.com/ https://example.com/?two)

chrome="$KOAN_ROOT/chromium/src/${KOAN_OUT:-out/Koan}/chrome"
export UI_PORT=${UI_PORT:-9600} CDP_PORT=${CDP_PORT:-9601}
prof=$(koan_new_profile)
pid=
cleanup() { [[ -n $pid ]] && kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; koan_rm "$prof" "$prof.log"; }
trap cleanup EXIT

"$chrome" "${KOAN_HEADLESS_UI_FLAGS[@]}" --user-data-dir="$prof" \
  --enable-ui-devtools="$UI_PORT" --remote-debugging-port="$CDP_PORT" \
  --enable-features=ui-debug-tools-enable-synthetic-events \
  "${urls[@]}" >"$prof.log" 2>&1 &
pid=$!
for _ in $(seq 1 60); do
  curl -sf "http://127.0.0.1:$CDP_PORT/json/version" >/dev/null && break
  sleep 0.5
done
sleep 3
node "$KOAN_ROOT/scripts/e2e/$test_name.mjs"
