#!/usr/bin/env bash
# Runs an end-to-end test against a real Koan build, off-screen.
#
#   scripts/e2e/run.sh <test> [url...]     e.g. scripts/e2e/run.sh shortcuts
#
# <test> is a name in scripts/e2e/ or a path to any .mjs script (e.g.
# scripts/perf/profile_ui.mjs). Without URLs it opens
# scripts/e2e/pages/<test>.html if that exists, else two example.com tabs.
# scripts/e2e/prefs/<test>.json, if it exists, seeds the fresh profile's
# Preferences. KOAN_FLAGS adds browser flags (e.g. --disable-features=...);
# features in an --enable-features there are added to the ones run.sh needs.
#
# The test gets KOAN_CHROME_ARGS, Koan's command line without the URLs (one
# argument per line), to restart Koan with the same profile.
#
# Starts Koan (out/Koan, or KOAN_OUT=out/Release) on the headless Ozone
# platform with a fresh profile, UI DevTools and the DevTools protocol, runs
# scripts/e2e/<test>.mjs, and cleans up. Nothing appears on the desktop.
set -euo pipefail

source "$(dirname "$0")/../koan_test_env.sh"
test_name=${1:?usage: run.sh <test> [url...]}
shift
urls=("$@")
if [[ ${#urls[@]} -eq 0 ]]; then
  page="$KOAN_ROOT/scripts/e2e/pages/$test_name.html"
  if [[ -f $page ]]; then
    urls=("file://$page")
  else
    urls=(https://example.com/ https://example.com/?two)
  fi
fi

chrome="$KOAN_ROOT/chromium/src/${KOAN_OUT:-out/Koan}/chrome"
export UI_PORT=${UI_PORT:-9600} CDP_PORT=${CDP_PORT:-9601}
prof=$(koan_new_profile)
pid=
cleanup() { [[ -n $pid ]] && kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; koan_rm "$prof" "$prof.log"; }
trap cleanup EXIT
prefs="$KOAN_ROOT/scripts/e2e/prefs/$test_name.json"
if [[ -f $prefs ]]; then
  mkdir -p "$prof/Default"
  cp "$prefs" "$prof/Default/Preferences"
fi

script="$KOAN_ROOT/scripts/e2e/$test_name.mjs"
[[ $test_name == *.mjs ]] && script=$test_name
# A second --enable-features would replace the first, so merge them.
features=ui-debug-tools-enable-synthetic-events
extra=()
for flag in ${KOAN_FLAGS:-}; do
  case $flag in
    --enable-features=*) features+=",${flag#--enable-features=}" ;;
    *) extra+=("$flag") ;;
  esac
done
args=("$chrome" "${KOAN_HEADLESS_UI_FLAGS[@]}" --user-data-dir="$prof"
  --enable-ui-devtools="$UI_PORT" --remote-debugging-port="$CDP_PORT"
  --enable-features="$features" "${extra[@]}")
KOAN_CHROME_ARGS=$(printf '%s\n' "${args[@]}")
export KOAN_CHROME_ARGS
"${args[@]}" "${urls[@]}" >"$prof.log" 2>&1 &
pid=$!
for _ in $(seq 1 60); do
  curl -sf "http://127.0.0.1:$CDP_PORT/json/version" >/dev/null && break
  sleep 0.5
done
sleep 3
node "$script"
