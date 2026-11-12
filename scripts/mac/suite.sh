#!/bin/bash
# suite.sh: Koan's scripted Mac test cycle, for the non-AI Mac queue. Builds
# nothing; tests an existing Koan.app and the test binaries next to it.
#
#   scripts/mac/suite.sh [options] <path/to/Koan.app>
#
# Runs, in order, each with a timeout and its full log in a file:
#   (a) the C++ test binaries in the app's out dir (koan_*unittests,
#       koan_browsertests) with --gtest_brief=1;
#   (b) the e2e scripts of scripts/mac/suite/e2e.list against a fresh
#       --user-data-dir each (--use-mock-keychain), through CDP and UI
#       DevTools; scripts/e2e/<name>.mac.flags, if it exists, replaces
#       <name>.flags (see flags_file_of());
#   (v) the visual checks: scripts/mac/visual/capture.mjs, then diff.py
#       against docs/screenshots/reference/ (--record writes them);
#   (c) crash reports: new .ips files under ~/Library/Logs/DiagnosticReports
#       from this app or out dir since the run started; and a grep of every
#       Koan log for FATAL, "Check failed" / "CHECK failed" and DCHECK.
# Prints ONE line per test, "PASS name" or "FAIL name: <first failing
# assertion or crash signature>" (a SKIP line when it could not run), then
# a TOTAL line. The same lines go to <logs>/summary.txt. Exit 0 = no FAIL.
#
# Options:
#   --out DIR        the out dir with the test binaries (default: the app's dir)
#   --logs DIR       where logs go (default: $KOAN_MAC_ROOT/suite/<time>,
#                    KOAN_MAC_ROOT defaulting to ~/work/koan-mac)
#   --only a,b       run only these tests (names as printed)
#   --skip-cxx | --skip-e2e | --skip-visual | --skip-crash
#   --all-e2e        every scripts/e2e/*.mjs instead of e2e.list
#   --record         visual: save the crops as the new reference images
#   --input auto|real|synthetic
#                    real pointer and keys for the e2e checks that need AppKit
#                    (auto, the default: only when the bench tools are in
#                    $KOAN_MAC_TOOLS, default $KOAN_MAC_ROOT/tmp, the screen
#                    is granted (grantok.sh) and nobody has used the Mac for
#                    600 s (usergate.sh)); synthetic never moves the pointer
#   --gtest-filter F passed to every C++ binary
#
# Safety: the runner starts Koan only with its own fresh profiles (under a
# short /tmp/ksuite.* dir, removed at the end), and stops only the processes
# whose command line has one of those profiles and whose executable is
# inside this app, by exact PID. It never touches other apps, another Koan or
# the user's profile. Before each launch it checks that nothing else listens
# on its debugging ports, so a test can never drive someone else's browser.
set -u
set -o pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
M="${KOAN_MAC_ROOT:-$HOME/work/koan-mac}"
TOOLS="${KOAN_MAC_TOOLS:-$M/tmp}"
UI_PORT=9640
CDP_PORT=9641

usage() { sed -n '2,/^set -u/p' "$0" | sed 's/^# \{0,1\}//; /^set -u/d'; exit 2; }

app="" out="" logs="" only="" record=0 input=auto all_e2e=0 gtest_filter=""
skip_cxx=0 skip_e2e=0 skip_visual=0 skip_crash=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) out="$2"; shift 2 ;;
    --logs) logs="$2"; shift 2 ;;
    --only) only=",$2,"; shift 2 ;;
    --skip-cxx) skip_cxx=1; shift ;;
    --skip-e2e) skip_e2e=1; shift ;;
    --skip-visual) skip_visual=1; shift ;;
    --skip-crash) skip_crash=1; shift ;;
    --all-e2e) all_e2e=1; shift ;;
    --record) record=1; shift ;;
    --input) input="$2"; shift 2 ;;
    --gtest-filter) gtest_filter="$2"; shift 2 ;;
    -h|--help) usage ;;
    -*) echo "suite.sh: unknown option $1" >&2; usage ;;
    *) app="$1"; shift ;;
  esac
done
[[ -n "$app" ]] || usage
app="$(cd "$app" 2>/dev/null && pwd)" || { echo "suite.sh: no app at $1" >&2; exit 2; }
plist="$app/Contents/Info.plist"
[[ -f "$plist" ]] || { echo "suite.sh: $app has no Contents/Info.plist" >&2; exit 2; }
exe="$app/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$plist")"
[[ -x "$exe" ]] || { echo "suite.sh: no executable $exe" >&2; exit 2; }
out="${out:-$(dirname "$app")}"
logs="${logs:-$M/suite/$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$logs"
summary="$logs/summary.txt"
: >"$summary"
# Short, so the agent-control socket path under a profile stays under 104.
work="$(mktemp -d /tmp/ksuite.XXXXXX)" || exit 2
start_mark="$logs/.start"
touch "$start_mark"
pass=0 fail=0 skip=0

# --- Reporting.
line() {  # <text>: to stdout and the summary
  echo "$1"
  echo "$1" >>"$summary"
}
report() {  # PASS|FAIL|SKIP <name> [detail]
  local detail="${3:-}"
  detail="$(printf '%s' "$detail" | tr '\n\t' '  ' | cut -c1-220)"
  case "$1" in
    PASS) pass=$((pass + 1)); line "PASS $2${detail:+ ($detail)}" ;;
    FAIL) fail=$((fail + 1)); line "FAIL $2: ${detail:-failed}" ;;
    SKIP) skip=$((skip + 1)); line "SKIP $2${detail:+: $detail}" ;;
  esac
}
wanted() { [[ -z "$only" || "$only" == *",$1,"* ]]; }

# --- Processes, by exact PID only.
descendants() {  # <pid>: its children, grandchildren, ...
  local child
  for child in $(pgrep -P "$1" 2>/dev/null); do
    echo "$child"
    descendants "$child"
  done
}
kill_tree() {  # <pid>: TERM it and what it started, then KILL what is left
  local pids p
  pids="$(descendants "$1") $1"
  for p in $pids; do kill -TERM "$p" 2>/dev/null; done
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    local alive=0
    for p in $pids; do kill -0 "$p" 2>/dev/null && alive=1; done
    [[ $alive == 0 ]] && return 0
    sleep 0.5
  done
  for p in $pids; do kill -KILL "$p" 2>/dev/null; done
}
# The PIDs of this app's processes that use profile <dir>: the command line
# has --user-data-dir=<dir> and the executable is inside this app.
koan_pids() {
  ps -Ao pid=,command= | awk -v prof="--user-data-dir=$1 " -v app="$app/Contents/" \
    'index($0 " ", prof) && index($0, app) { print $1 }'
}
stop_koans() {  # <profile dir>
  local pids p
  pids="$(koan_pids "$1")"
  [[ -z "$pids" ]] && return 0
  for p in $pids; do kill -TERM "$p" 2>/dev/null; done
  for _ in $(seq 1 30); do
    [[ -z "$(koan_pids "$1")" ]] && return 0
    sleep 0.5
  done
  for p in $(koan_pids "$1"); do kill -KILL "$p" 2>/dev/null; done
}
# Runs <cmd...> with output to <log>, at most <secs>; 124 on a timeout.
run_timeout() {  # <secs> <log> <cmd...>
  local secs="$1" log="$2" p waited=0
  shift 2
  "$@" >"$log" 2>&1 &
  p=$!
  while kill -0 "$p" 2>/dev/null; do
    if (( waited >= secs )); then
      echo "suite.sh: timed out after ${secs}s" >>"$log"
      kill_tree "$p"
      wait "$p" 2>/dev/null
      return 124
    fi
    sleep 1
    waited=$((waited + 1))
  done
  wait "$p"
}
cleanup() {
  local prof
  for prof in "$work"/p*; do
    [[ -d "$prof" ]] && stop_koans "$prof"
  done
  rm -rf "$work"
}
trap cleanup EXIT
trap 'echo "suite.sh: stopped" >&2; exit 130' INT TERM

# The first thing that went wrong, from a log.
crash_line() {  # <log>
  grep -m1 -E 'Check failed|CHECK failed|DCHECK|FATAL|Received signal|Segmentation fault|Abort trap' "$1" 2>/dev/null |
    sed -E 's/^\[[^]]*\] ?//'
}
gtest_failure() {  # <log>
  local where
  where="$(grep -m1 -A2 -E ': Failure$' "$1" | paste -sd' ' - | sed -E 's/ +/ /g')"
  [[ -n "$where" ]] && { echo "${where#"$ROOT/"}"; return; }
  where="$(grep -m1 -E '^\[  FAILED  \]' "$1")"
  [[ -n "$where" ]] && { echo "$where"; return; }
  crash_line "$1"
}

# --- Real input: only when the bench says the Mac is free.
input_mode=synthetic
input_why="synthetic (--input synthetic)"
if [[ "$input" != synthetic ]]; then
  if [[ ! -x "$TOOLS/keypress2" || ! -x "$TOOLS/ptr" || ! -x "$TOOLS/inputok.sh" ]]; then
    input_why="synthetic (no keypress2/ptr/inputok.sh in $TOOLS)"
  elif ! "$TOOLS/grantok.sh" 2>/dev/null; then
    input_why="synthetic (the screen is not granted: grantok.sh)"
  elif [[ -x "$TOOLS/usergate.sh" ]] && (( $("$TOOLS/usergate.sh") < 600 )); then
    input_why="synthetic (someone used the Mac in the last 600 s)"
  else
    input_mode=real
    input_why="real (bench tools in $TOOLS)"
  fi
  if [[ "$input" == real && "$input_mode" != real ]]; then
    echo "suite.sh: --input real refused: $input_why" >&2
    exit 3
  fi
fi

line "suite: $app"
line "suite: input $input_why; logs in $logs"

# --- (a) C++ test binaries.
if [[ $skip_cxx == 0 ]]; then
  found=0
  for bin in "$out"/koan_*unittests "$out"/koan_browsertests; do
    [[ -x "$bin" && -f "$bin" ]] || continue
    name="$(basename "$bin")"
    found=1
    wanted "$name" || continue
    secs=900
    [[ "$name" == koan_browsertests ]] && secs=3600
    args=(--gtest_brief=1)
    [[ -n "$gtest_filter" ]] && args+=(--gtest_filter="$gtest_filter")
    log="$logs/$name.log"
    run_timeout "$secs" "$log" "$bin" "${args[@]}"
    status=$?
    if [[ $status == 0 ]]; then
      report PASS "$name" "$(grep -m1 -E '^\[==========\] [0-9]+ tests? from' "$log" | sed -E 's/^\[=+\] //')"
    elif [[ $status == 124 ]]; then
      first="$(gtest_failure "$log")"
      report FAIL "$name" "timed out after ${secs}s${first:+; $first}"
    else
      report FAIL "$name" "$(gtest_failure "$log" || true)${status:+ (exit $status)}"
    fi
  done
  [[ $found == 0 ]] && report SKIP "C++ tests" "no koan_*unittests or koan_browsertests in $out"
fi

# --- Koan for one e2e script.
# The flags file of e2e script <name> on the Mac: scripts/e2e/<name>.mac.flags
# if it exists (flags only the Mac run takes, e.g. the test_helper
# extension), else <name>.flags (which may not exist). The skip markers and
# needs_ozone() below read the same file.
flags_file_of() {  # <name>
  local mac="$ROOT/scripts/e2e/$1.mac.flags"
  if [[ -f "$mac" ]]; then echo "$mac"; else echo "$ROOT/scripts/e2e/$1.flags"; fi
}
# Whether a script's .flags need Linux's headless Ozone ({dump}, --ozone-*),
# unless the file says the script runs on macOS without them (a line
# "# macos: without frame dumps" or "# macOS: run"; the script reads the
# window another way there, e.g. KOAN_DUMP_DIR or a screen capture).
needs_ozone() {  # <flags file>
  [[ -f "$1" ]] && grep -qE '\{dump\}|--ozone' "$1" &&
    ! grep -qxiE '# macos: (without frame dumps|run)' "$1"
}
serial=0
# Launches Koan with a fresh profile for test <name>, runs node <script>
# with a timeout, stops that Koan. Sets $e2e_status and $e2e_log.
run_koan_script() {  # <name> <script> <secs> [extra flags...]
  local name="$1" script="$2" secs="$3"
  shift 3
  serial=$((serial + 1))
  local prof="$work/p$serial" agent_home="$work/h$serial"
  mkdir -p "$prof/Default" "$agent_home"
  e2e_log="$logs/e2e-$name.log"
  local koan_log="$logs/koan-$name.log"
  local prefs="$ROOT/scripts/e2e/prefs/$name.json" local_state="$ROOT/scripts/e2e/local_state/$name.json"
  # prefs/<name>.mac.json, if it exists, replaces prefs/<name>.json here
  # (a setting that means something else on the Mac, e.g. a click modifier).
  [[ -f "$ROOT/scripts/e2e/prefs/$name.mac.json" ]] && prefs="$ROOT/scripts/e2e/prefs/$name.mac.json"
  [[ -f "$prefs" ]] && sed "s|{profile}|$prof|g" "$prefs" >"$prof/Default/Preferences"
  [[ -f "$local_state" ]] && cp "$local_state" "$prof/Local State"
  local enable="ui-debug-tools-enable-synthetic-events" disable="MacAppCodeSignClone" extra=() flag
  local flags_file
  flags_file="$(flags_file_of "$name")"
  local file_flags=() updates=(--disable-component-update)
  if [[ -f "$flags_file" ]]; then
    while IFS= read -r flag || [[ -n "$flag" ]]; do
      [[ -z "$flag" || "$flag" == \#* ]] && continue
      # Frame dumps are headless Ozone's: a script that runs without them
      # (needs_ozone() above) gets the rest of its flags.
      [[ "$flag" == *"{dump}"* || "$flag" == --ozone* ]] && continue
      # {component-update}: let the component updater run (widevine: it
      # downloads the CDM). Every other test keeps it off.
      if [[ "$flag" == "{component-update}" ]]; then updates=(); continue; fi
      # {root}: the checkout; {profile}: this run's user data dir.
      flag="${flag//\{root\}/$ROOT}"
      file_flags+=("${flag//\{profile\}/$prof}")
    done <"$flags_file"
  fi
  # KOAN_FLAGS is a list of flags, split on spaces as in run.sh.
  # shellcheck disable=SC2086
  for flag in ${KOAN_FLAGS:-} ${file_flags[@]+"${file_flags[@]}"} "$@"; do
    case "$flag" in
      --enable-features=*) enable+=",${flag#--enable-features=}" ;;
      --disable-features=*) disable+=",${flag#--disable-features=}" ;;
      *) extra+=("$flag") ;;
    esac
  done
  local port
  for port in "$UI_PORT" "$CDP_PORT"; do
    if [[ -n "$(lsof -nP -iTCP:"$port" -sTCP:LISTEN -t 2>/dev/null)" ]]; then
      echo "suite.sh: port $port is in use by another process; not starting Koan" >"$e2e_log"
      e2e_status=90
      return
    fi
  done
  local page="$ROOT/scripts/e2e/pages/$name.html" urls=(https://example.com/ https://example.com/?two)
  [[ -f "$page" ]] && urls=("file://$page")
  # shellcheck disable=SC2054  # "1280,800" is one value
  local args=("$exe" --user-data-dir="$prof" --no-first-run --no-default-browser-check
    ${updates[@]+"${updates[@]}"} --use-mock-keychain --window-size=1280,800
    --enable-logging=stderr --enable-ui-devtools="$UI_PORT" --remote-debugging-port="$CDP_PORT"
    --enable-features="$enable" --disable-features="$disable" ${extra[@]+"${extra[@]}"})
  "${args[@]}" "${urls[@]}" >"$koan_log" 2>&1 &
  local pid=$!
  local up=0
  for _ in $(seq 1 80); do
    if curl -sf --max-time 2 "http://127.0.0.1:$CDP_PORT/json/version" >/dev/null 2>&1; then
      up=1
      break
    fi
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.5
  done
  # The port must be this Koan's (its exact pid), never another browser's.
  local listener
  listener="$(lsof -nP -iTCP:"$CDP_PORT" -sTCP:LISTEN -t 2>/dev/null | head -1)"
  if [[ $up == 0 || "$listener" != "$pid" ]]; then
    {
      echo "suite.sh: Koan did not come up (pid $pid, CDP listener '${listener}')"
      tail -n 30 "$koan_log"
    } >"$e2e_log"
    e2e_status=91
  else
    sleep 3
    KOAN_CHROME_ARGS="$(printf '%s\n' "${args[@]}")" KOAN_LOG="$koan_log" KOAN_PID="$pid" \
      KOAN_PROFILE="$prof" KOAN_APP="$app" KOAN_CLI="$app/Contents/Resources/bin/koan" \
      KOAN_AGENT_HOME="$agent_home" KOAN_MAC_TOOLS="$TOOLS" KOAN_INPUT="$input_mode" \
      UI_PORT="$UI_PORT" CDP_PORT="$CDP_PORT" KOAN_ROOT="$ROOT" \
      run_timeout "$secs" "$e2e_log" node "$script"
    e2e_status=$?
  fi
  stop_koans "$prof"
  wait "$pid" 2>/dev/null
  rm -rf "$prof" "$agent_home"
}
# Reports an e2e run: PASS (with its skips), SKIP (exit 77) or FAIL with the
# first failing check, else what the Koan log says, else the exit code.
report_e2e() {  # <name> <secs>
  local name="$1" secs="$2" first skips
  skips="$(grep -c '^SKIP' "$e2e_log" 2>/dev/null)"
  case "$e2e_status" in
    0) report PASS "$name" "$([[ ${skips:-0} -gt 0 ]] && echo "$skips skipped: $(grep -m1 '^SKIP' "$e2e_log" | sed 's/^SKIP *//')")" ;;
    77) report SKIP "$name" "$(grep -m1 '^SKIP' "$e2e_log" | sed 's/^SKIP *//')" ;;
    90|91) report FAIL "$name" "$(head -1 "$e2e_log")" ;;
    *)
      first="$(grep -m1 -E '^FAIL' "$e2e_log" | sed -E 's/^FAIL +//')"
      [[ -z "$first" ]] && first="$(crash_line "$logs/koan-$name.log")"
      [[ -z "$first" ]] && first="$(grep -m1 -E 'Error|error' "$e2e_log")"
      [[ $e2e_status == 124 ]] && first="timed out after ${secs}s${first:+; $first}"
      report FAIL "$name" "${first:-exit $e2e_status}"
      ;;
  esac
}

# --- (b) e2e scripts.
if [[ $skip_e2e == 0 ]]; then
  if ! command -v node >/dev/null; then
    report SKIP "e2e" "node is not installed"
  else
    tests=()
    if [[ $all_e2e == 1 ]]; then
      for f in "$ROOT"/scripts/e2e/*.mjs; do tests+=("$(basename "$f" .mjs)"); done
    else
      while read -r name rest || [[ -n "$name" ]]; do
        [[ -z "$name" || "$name" == \#* ]] && continue
        tests+=("$name${rest:+ $rest}")
      done <"$HERE/suite/e2e.list"
    fi
    for entry in ${tests[@]+"${tests[@]}"}; do
      name="${entry%% *}"
      secs=300
      [[ "$entry" == *timeout=* ]] && secs="${entry##*timeout=}" && secs="${secs%% *}"
      wanted "$name" || continue
      # A script's .flags may say what it does here: "# macOS: skip: <why>",
      # or that it runs without Linux's headless Ozone (needs_ozone()).
      flags_file="$(flags_file_of "$name")"
      if [[ -f "$flags_file" ]] && grep -q '^# macOS: skip: ' "$flags_file"; then
        report SKIP "$name" "$(grep -m1 '^# macOS: skip: ' "$flags_file" | sed 's/^# macOS: skip: //')"
        continue
      fi
      if needs_ozone "$flags_file"; then
        report SKIP "$name" "needs Linux's headless Ozone (its .flags)"
        continue
      fi
      run_koan_script "$name" "$ROOT/scripts/e2e/$name.mjs" "$secs"
      report_e2e "$name" "$secs"
    done
  fi
fi

# --- (v) Visual checks.
if [[ $skip_visual == 0 ]] && wanted visual; then
  winid="$TOOLS/winid"
  if [[ ! -x "$winid" ]]; then
    winid="$work/winid"
    clang -fobjc-arc -framework CoreGraphics -framework Foundation -o "$winid" \
      "$HERE/tools/winid.m" >"$logs/winid-build.log" 2>&1 || winid=""
  fi
  if [[ -z "$winid" ]]; then
    report SKIP visual "could not build scripts/mac/tools/winid.m (see winid-build.log)"
  else
    shots="$logs/visual"
    mkdir -p "$shots"
    KOAN_SHOTS="$shots" KOAN_WINID="$winid" run_koan_script visual "$HERE/visual/capture.mjs" 240
    if [[ $e2e_status != 0 ]]; then
      report_e2e visual 240
    else
      diff_args=(--shots "$shots")
      [[ $record == 1 ]] && diff_args+=(--record)
      python3 -I "$HERE/visual/diff.py" "${diff_args[@]}" >"$logs/visual-diff.log" 2>&1
      status=$?
      regions="$(grep -cE '^(PASS|FAIL|RECORD)' "$logs/visual-diff.log")"
      if [[ $record == 1 && $status == 0 ]]; then
        report PASS visual "recorded $regions references in docs/screenshots/reference/"
      elif [[ $status == 0 ]]; then
        noref="$(grep -c '^NOREF' "$logs/visual-diff.log")"
        if [[ "$regions" == 0 ]]; then
          report SKIP visual "no reference images yet (run suite.sh --record once)"
        else
          report PASS visual "$regions regions${noref:+, $noref without a reference}"
        fi
      else
        report FAIL visual "$(grep -m1 '^FAIL' "$logs/visual-diff.log" | sed 's/^FAIL //')"
      fi
    fi
  fi
fi

# --- (c) Crash reports and log greps.
if [[ $skip_crash == 0 ]]; then
  if wanted crash-reports; then
    reports="$HOME/Library/Logs/DiagnosticReports"
    crashes=()
    ncrashes=0
    while IFS= read -r f; do
      [[ -n "$f" ]] || continue
      # Only this app's processes and this out dir's test binaries.
      if grep -qF -e "$app/" -e "$out/" "$f" 2>/dev/null; then
        crashes+=("$f")
        ncrashes=$((ncrashes + 1))
      fi
    done < <(find "$reports" -maxdepth 2 -name '*.ips' -newer "$start_mark" 2>/dev/null)
    if [[ $ncrashes == 0 ]]; then
      report PASS crash-reports "no new .ips from this app"
    else
      for f in "${crashes[@]}"; do cp "$f" "$logs/" 2>/dev/null; done
      sig="$(python3 -I - "${crashes[0]}" <<'PY'
import json, sys
path = sys.argv[1]
with open(path) as f:
    header = json.loads(f.readline())
    try:
        body = json.load(f)
    except ValueError:
        body = {}
exc = body.get('exception', {})
frame = ''
threads = body.get('threads') or []
fault = body.get('faultingThread', 0)
if isinstance(fault, int) and fault < len(threads):
    frames = threads[fault].get('frames') or []
    for fr in frames[:8]:
        sym = fr.get('symbol')
        if sym:
            frame = sym
            break
print('%s: %s %s%s' % (header.get('app_name', body.get('procName', '?')),
                       exc.get('type', body.get('termination', {}).get('indicator', '?')),
                       exc.get('signal', ''), (' in ' + frame) if frame else ''))
PY
)"
      report FAIL crash-reports "$ncrashes new: $(basename "${crashes[0]}"): $sig"
    fi
  fi
  if wanted log-grep; then
    hit=""
    for f in "$logs"/koan-*.log; do
      [[ -f "$f" ]] || continue
      l="$(crash_line "$f")"
      if [[ -n "$l" ]]; then
        hit="$(basename "$f" .log | sed 's/^koan-//'): $l"
        break
      fi
    done
    if [[ -z "$hit" ]]; then
      report PASS log-grep "no FATAL, Check failed or DCHECK in the Koan logs"
    else
      report FAIL log-grep "$hit"
    fi
  fi
fi

line "TOTAL $pass passed, $fail failed, $skip skipped (logs: $logs)"
[[ $fail == 0 ]]
