#!/bin/bash
# launch.sh [<Koan.app>] [-- extra flags]: starts a Koan of our own for a
# video run: a fresh profile under $KOAN_MAC_ROOT/rec-<stamp> (never the
# user's), UI DevTools and CDP on their own ports, the e2e test_helper
# extension, a fixed window position and size (so record.sh's crop matches
# the window), and prints the environment a flow needs (also written to
# <rec dir>/koan.env):
#   KOAN_PID UI_PORT CDP_PORT KOAN_LOG KOAN_PROFILE REC_DIR KOAN_MAC_TOOLS KOAN_INPUT
# It refuses to start when a port is taken, and checks that the CDP port is
# answered by exactly the pid it started. Stop it with quit.sh <pid>.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$here/../../.." && pwd)"
M="${KOAN_MAC_ROOT:-$HOME/work/koan-mac}"
app="${1:-$M/koan/chromium/src/out/Koan/Koan.app}"
[[ $# -gt 0 ]] && shift
[[ "${1:-}" == "--" ]] && shift
UI_PORT="${UI_PORT:-9740}"
CDP_PORT="${CDP_PORT:-9741}"
# The window, in screen points (the bench screen is 1470x956).
POS="${KOAN_WINDOW_POS:-60,40}"
SIZE="${KOAN_WINDOW_SIZE:-1280,820}"

plist="$app/Contents/Info.plist"
[[ -f "$plist" ]] || { echo "launch.sh: no app at $app" >&2; exit 2; }
exe="$app/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")"
for port in "$UI_PORT" "$CDP_PORT"; do
  if [[ -n "$(lsof -nP -iTCP:"$port" -sTCP:LISTEN -t 2>/dev/null)" ]]; then
    echo "launch.sh: port $port is in use by another process; not starting Koan" >&2
    exit 3
  fi
done

rec="$M/rec-$(date +%Y%m%d-%H%M%S)"
prof="$rec/profile"
mkdir -p "$prof/Default"
log="$rec/koan.log"
# shellcheck disable=SC2054  # "1280,820" is one value
args=("$exe" --user-data-dir="$prof" --no-first-run --no-default-browser-check
  --disable-component-update --use-mock-keychain
  --window-position="$POS" --window-size="$SIZE"
  --enable-logging=stderr --enable-ui-devtools="$UI_PORT" --remote-debugging-port="$CDP_PORT"
  --load-extension="$ROOT/scripts/e2e/extensions/test_helper"
  --enable-features=ui-debug-tools-enable-synthetic-events
  --disable-features=MacAppCodeSignClone,KoanWelcome
  "$@")
"${args[@]}" "about:blank" >"$log" 2>&1 &
pid=$!
up=0
for _ in $(seq 1 80); do
  if curl -sf --max-time 2 "http://127.0.0.1:$CDP_PORT/json/version" >/dev/null 2>&1; then
    up=1
    break
  fi
  kill -0 "$pid" 2>/dev/null || break
  sleep 0.5
done
listener="$(lsof -nP -iTCP:"$CDP_PORT" -sTCP:LISTEN -t 2>/dev/null | head -1)"
if [[ $up == 0 || "$listener" != "$pid" ]]; then
  echo "launch.sh: Koan did not come up (pid $pid, CDP listener '${listener}')" >&2
  tail -n 20 "$log" >&2
  [[ $up == 0 ]] && kill -0 "$pid" 2>/dev/null && "$here/quit.sh" "$pid" >&2
  exit 4
fi
cat >"$rec/koan.env" <<ENV
KOAN_PID=$pid
UI_PORT=$UI_PORT
CDP_PORT=$CDP_PORT
KOAN_LOG=$log
KOAN_PROFILE=$prof
REC_DIR=$rec
KOAN_MAC_TOOLS=${KOAN_MAC_TOOLS:-$M/tmp}
KOAN_INPUT=real
ENV
cat "$rec/koan.env"
