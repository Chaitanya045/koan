#!/bin/bash
# remote_debugging.sh <Koan.app path on the Mac>
# Behaviour test for patches/chrome/browser/devtools/remote_debugging_server.cc.patch:
# Koan refuses --remote-debugging-port / --remote-debugging-pipe on its default user
# data directory, and still serves them on any other one. Run on the Mac, by the Mac loop,
# after its usual gates (the user is away; Koan windows are not shown: --no-startup-window).
#
#   leg A  always:   fresh --user-data-dir, --remote-debugging-port=0 -> DevToolsActivePort is
#                    written and /json/version answers.
#   leg B  opt-in:   KOAN_DEFAULT_DIR_OK=1. Starts Koan on the REAL default profile (no window),
#                    port 9222 and then the pipe: both must be refused with the stderr message
#                    "requires a non-default data directory", and nothing may listen on 9222.
#                    Skipped (not failed) when a Koan from this app is already running, because
#                    a second launch would only hand over to the running one and test nothing.
#
# Exit 0 = every leg that ran passed. Output: one PASS/FAIL/SKIP line per leg.
set -u

app="${1:?usage: remote_debugging.sh <Koan.app>}"
exe="$app/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app/Contents/Info.plist")"
out="$(mktemp -d "${TMPDIR:-/tmp}/koan-rd.XXXXXX")"
flags=(--no-first-run --no-startup-window --disable-features=MacAppCodeSignClone)
status=0
pid=""

stop_koan() {
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    kill -TERM "$pid" 2>/dev/null
    for _ in $(seq 1 40); do kill -0 "$pid" 2>/dev/null || break; sleep 0.25; done
    kill -KILL "$pid" 2>/dev/null
  fi
  pid=""
}
trap 'stop_koan' EXIT

report() {  # PASS|FAIL|SKIP <name> [detail]
  echo "$1  $2${3:+: $3}"
  [[ "$1" == FAIL ]] && status=1
}

# Leg A: a non-default directory still gets its server.
profile="$out/profile"
"$exe" "${flags[@]}" --user-data-dir="$profile" --remote-debugging-port=0 \
  >"$out/a.out" 2>"$out/a.err" 3<&- 4<&- &
pid=$!
port=""
for _ in $(seq 1 120); do
  if [[ -s "$profile/DevToolsActivePort" ]]; then
    port="$(head -n1 "$profile/DevToolsActivePort")"
    break
  fi
  sleep 0.25
done
if [[ -z "$port" ]]; then
  report FAIL "A non-default dir serves --remote-debugging-port" "no DevToolsActivePort in 30 s ($out/a.err)"
elif curl -sf --max-time 5 "http://127.0.0.1:$port/json/version" | grep -q '"Browser"'; then
  report PASS "A non-default dir serves --remote-debugging-port" "port $port"
else
  report FAIL "A non-default dir serves --remote-debugging-port" "/json/version did not answer on $port"
fi
stop_koan

# Leg B: the default directory does not.
if [[ "${KOAN_DEFAULT_DIR_OK:-}" != 1 ]]; then
  report SKIP "B default dir refuses the switches" "set KOAN_DEFAULT_DIR_OK=1 (it starts Koan on the real profile)"
elif pgrep -f "$exe" >/dev/null; then
  report SKIP "B default dir refuses the switches" "a Koan from this app is already running"
else
  for sw in --remote-debugging-port=9222 --remote-debugging-pipe; do
    name="B default dir refuses $sw"
    "$exe" "${flags[@]}" "$sw" >"$out/b.out" 2>"$out/b.err" 3<&- 4<&- &
    pid=$!
    seen=""
    for _ in $(seq 1 120); do
      if grep -q 'requires a non-default data directory' "$out/b.err"; then seen=1; break; fi
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.25
    done
    listening=""
    if curl -s --max-time 2 "http://127.0.0.1:9222/json/version" >/dev/null 2>&1; then listening=1; fi
    stop_koan
    if [[ -z "$seen" ]]; then
      report FAIL "$name" "no refusal message on stderr ($out/b.err)"
    elif [[ -n "$listening" ]]; then
      report FAIL "$name" "something answers on 127.0.0.1:9222"
    else
      report PASS "$name"
    fi
  done
fi

echo "logs: $out"
exit "$status"
