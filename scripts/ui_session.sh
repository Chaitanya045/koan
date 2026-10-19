#!/usr/bin/env bash
# ui_session.sh <port> <seconds> [extra chrome flags...] -- <urls...>
# Runs Koan off-screen (headless Ozone) with UI DevTools on <port> for
# <seconds>, dumping frames to $KOAN_TEST_DIR/session-<port>/ (latest frame
# copied to frame.png on exit). Drive it meanwhile with ui_tree.mjs /
# ui_input.mjs. Run it in the background. PREFS=<json> seeds Preferences.
source "$(dirname "$0")/koan_test_env.sh"
port=$1; secs=$2; shift 2
flags=(); while [[ $# -gt 0 && $1 != -- ]]; do flags+=("$1"); shift; done; shift
prof=$(koan_new_profile); sess="$KOAN_TEST_DIR/session-$port"
rm -rf "$sess"; mkdir -p "$sess/dump"
pid=
cleanup() { [[ -n $pid ]] && kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; koan_rm "$prof"; }
trap cleanup EXIT
if [[ -n ${PREFS:-} ]]; then mkdir -p "$prof/Default"; printf '%s' "$PREFS" > "$prof/Default/Preferences"; fi
"$KOAN_CHROME" "${KOAN_HEADLESS_UI_FLAGS[@]}" --ozone-dump-file="$sess/dump" \
  --enable-ui-devtools="$port" --user-data-dir="$prof" \
  --enable-features=ui-debug-tools-enable-synthetic-events "${flags[@]}" "$@" \
  >"$sess/chrome.log" 2>&1 &
pid=$!
sleep "$secs"
