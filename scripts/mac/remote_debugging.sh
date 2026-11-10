#!/bin/bash
# remote_debugging.sh <Koan.app path on the Mac>
# Behaviour test for patches/chrome/browser/devtools/remote_debugging_server.cc.patch:
# Koan refuses --remote-debugging-port / --remote-debugging-pipe on its default user
# data directory, and still serves them on any other one. Run on the Mac, by the Mac loop,
# after its usual gates (the user is away). Koan starts with --no-startup-window, so nothing
# is drawn.
#
#   leg A  non-default dir (real HOME): --user-data-dir=<fresh> --remote-debugging-port=0
#          -> DevToolsActivePort is written and /json/version answers.
#   leg B  the DEFAULT dir, under a THROW-AWAY HOME, so the real default dir is never touched.
#          Koan's default dir is $HOME/Library/Application Support/Koan, resolved through
#          Foundation (NSSearchPath), which follows $HOME for a process started directly (not
#          through LaunchServices). Before leg B runs, a probe checks that CoreFoundation
#          honours the HOME override (`defaults write` lands under the throw-away HOME, not the
#          real one); if it does not, leg B is SKIPPED, never run on the real profile. After
#          each run the script checks that the throw-away default dir was the one used, and that
#          the real default dir's `Local State` is unchanged. Both switches must be refused with
#          "requires a non-default data directory", and nothing may listen on 9222.
#          Koan runs with --use-mock-keychain (best effort) so the missing login keychain under
#          the fake HOME cannot raise a dialog.
#   leg C  informational, also under the throw-away HOME: the default dir spelled in another
#          case (--user-data-dir=".../library/application support/koan"). APFS is
#          case-insensitive by default, and upstream compares paths as strings, so this is
#          expected to BYPASS the check (the same gap Chrome has). Prints INFO, never fails.
#
# Exit 0 = every PASS/FAIL leg passed. Output: one PASS/FAIL/SKIP/INFO line per leg.
set -u

app="${1:?usage: remote_debugging.sh <Koan.app>}"
exe="$app/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app/Contents/Info.plist")"
out="$(mktemp -d "${TMPDIR:-/tmp}/koan-rd.XXXXXX")"
real_home="$HOME"
fake_home="$out/home"
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

report() {  # PASS|FAIL|SKIP|INFO <name> [detail]
  echo "$1  $2${3:+: $3}"
  [[ "$1" == FAIL ]] && status=1
}

mtime() { stat -f %m "$1" 2>/dev/null || echo none; }

# Leg A: a non-default directory still gets its server.
profile="$out/profile"
"$exe" "${flags[@]}" --user-data-dir="$profile" --remote-debugging-port=0 \
  >"$out/a.out" 2>"$out/a.err" &
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

# Does the HOME override reach CoreFoundation/Foundation? (writes only a throw-away domain)
mkdir -p "$fake_home/Library/Preferences"
probe="com.koan.rdprobe.$$"
HOME="$fake_home" defaults write "$probe" k 1 >/dev/null 2>&1
home_honoured=""
if [[ -f "$fake_home/Library/Preferences/$probe.plist" && ! -f "$real_home/Library/Preferences/$probe.plist" ]]; then
  home_honoured=1
fi
defaults delete "$probe" >/dev/null 2>&1   # in case it landed in the real home
rm -f "$fake_home/Library/Preferences/$probe.plist"

real_dir="$real_home/Library/Application Support/Koan"
fake_dir="$fake_home/Library/Application Support/Koan"

# Did Koan work under the throw-away HOME? Its default dir is created when the
# path is first resolved, before the debugging switches are looked at; but
# rather than depend on that, any entry named Koan under the fake Library
# (Application Support, Caches, Logs, crash reports) counts.
used_fake_home() {
  [[ -d "$fake_dir" ]] ||
    [[ -n "$(find "$fake_home/Library" -maxdepth 3 -iname 'Koan*' -print -quit 2>/dev/null)" ]]
}

# Runs Koan once under the fake HOME with the given arguments and file-descriptor setup.
#   run_default <label> <stderr file> <fd mode: pipe|none> <args...>
run_default() {
  local err="$2" fdmode="$3"; shift 3
  if [[ "$fdmode" == pipe ]]; then
    # --remote-debugging-pipe needs fds 3 and 4 open before anything else runs
    # (chrome_main_delegate.cc), or it exits early: give it open, unused ones.
    env HOME="$fake_home" "$exe" "${flags[@]}" --use-mock-keychain "$@" \
      >"$out/b.out" 2>"$err" 3</dev/null 4>/dev/null &
  else
    env HOME="$fake_home" "$exe" "${flags[@]}" --use-mock-keychain "$@" \
      >"$out/b.out" 2>"$err" &
  fi
  pid=$!
}

# Leg B: the default directory does not get a server (under a throw-away HOME).
if [[ -z "$home_honoured" ]]; then
  report SKIP "B default dir refuses the switches" "HOME override not honoured here; not running on the real profile"
  report SKIP "C case-spelled default dir" "same reason"
else
  real_before="$(mtime "$real_dir/Local State")"
  for sw in --remote-debugging-port=9222 --remote-debugging-pipe; do
    name="B default dir refuses $sw"
    fdmode=none; [[ "$sw" == --remote-debugging-pipe ]] && fdmode=pipe
    err="$out/b-${sw#--remote-debugging-}.err"
    run_default B "$err" "$fdmode" "$sw"
    seen=""
    for _ in $(seq 1 120); do
      if grep -q 'requires a non-default data directory' "$err"; then seen=1; break; fi
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.25
    done
    listening=""
    if curl -s --max-time 2 "http://127.0.0.1:9222/json/version" >/dev/null 2>&1; then listening=1; fi
    stop_koan
    if ! used_fake_home; then
      report FAIL "$name" "nothing named Koan appeared under the throw-away HOME: the run may have hit the real profile"
    elif [[ "$(mtime "$real_dir/Local State")" != "$real_before" ]]; then
      report FAIL "$name" "the real default profile changed during the run"
    elif [[ -z "$seen" ]]; then
      report FAIL "$name" "no refusal message on stderr ($err)"
    elif [[ -n "$listening" ]]; then
      report FAIL "$name" "something answers on 127.0.0.1:9222"
    else
      report PASS "$name"
    fi
  done

  # Leg C: informational. The default dir spelled in lower case, under the fake HOME.
  if [[ ! -e "$fake_home/library" ]]; then
    report INFO "C case-spelled default dir" "this volume is case-sensitive; not applicable"
  else
    alt="$fake_home/library/application support/koan"
    run_default C "$out/c.err" none --user-data-dir="$alt" --remote-debugging-port=0
    got=""
    for _ in $(seq 1 60); do
      if [[ -s "$fake_dir/DevToolsActivePort" ]]; then got=1; break; fi
      if grep -q 'requires a non-default data directory' "$out/c.err"; then break; fi
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.25
    done
    stop_koan
    if [[ -n "$got" ]]; then
      report INFO "C case-spelled default dir" "BYPASSES the check (a server started); the same gap as upstream Chrome, see SECURITY.md"
    else
      report INFO "C case-spelled default dir" "refused (no server started)"
    fi
  fi
fi

echo "logs: $out"
exit "$status"
