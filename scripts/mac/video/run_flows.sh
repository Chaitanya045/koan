#!/bin/bash
# run_flows.sh: runs the user flows (flows/uf_*.mjs; the test cases are in
# .agent/notes/user-flows.md) on the Mac bench, each against a fresh Koan of
# our own, recorded, and writes one directory whose index.md is the only
# thing to read first.
#
#   scripts/mac/video/run_flows.sh [--app <Koan.app>] [--only uf_a,uf_b]
#       [--out <dir>] [--keep-raw] [--no-video] [--note <text>]
#       [--build-app <signed Koan.app>]
#
# --build-app: the app whose signature and bundle uf_build checks (read
# only, never launched), by default --app; a dev build (out/Koan) is ad hoc
# signed, and uf_build's signature steps SKIP on it, saying so.
#
# Per flow: launch.sh (a fresh profile under rec-*, Agent ready on through
# its Local State, the flow's prefs and extensions), record.sh (the window,
# 30 fps), the flow (node, at most $UF_FLOW_SECS, 300 s), record.sh stop,
# quit.sh <its exact pid>; then the checks on the recording (uf_post.mjs:
# compact mode's black-edge check, the still check of the person's page
# during the agent's captures), contact sheets at the marks (frames.sh, at
# $UF_SHEET_FPS fps, then compress.sh --sheet), and the video compressed to
# 720p (compress.sh --check). Screenshots are compressed by the flows
# themselves (within 1920x1080, PNG).
#
# Out (default $KOAN_MAC_ROOT/uf/<time>): index.md; per flow <flow>/ with
# summary.json, flow.log, marks.json, shots/, sheets/, video.720.mp4 and the
# launch's koan.log. --keep-raw keeps the full-size video and screenshots.
#
# The gates, as for every bench run (scripts/mac/video/README.md): the
# whole run holds the Mac's build/test lock ($KOAN_MAC_ROOT/tmp/e2e.lock,
# flock(2) through scripts/mac/tools/flock_run.py: the same lock as the Mac
# loop's flock.py stand-in, build_official.sh and the other bench runs);
# nobody used the Mac for 600 s (usergate.sh) and the screen is granted,
# checked before the run and before each flow; the flows check our Koan's
# pid is frontmost and that no input but ours came, before every pointer
# event and key. A flow stopped by a gate stops the run (the user is back).
# Never touches the user's Koan: our own app copy's processes only, by pid.
#
# Exit: 0 every flow passed (skips aside), 1 a flow failed, 3 stopped by a
# gate or the lock, 2 bad arguments.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$here/../../.." && pwd)"
M="${KOAN_MAC_ROOT:-$HOME/work/koan-mac}"
TOOLS="${KOAN_MAC_TOOLS:-$M/tmp}"
export PATH="/opt/homebrew/bin:$PATH"
NODE="${NODE:-$(command -v node || echo /opt/homebrew/bin/node)}"
FLOW_SECS="${UF_FLOW_SECS:-300}"
SHEET_FPS="${UF_SHEET_FPS:-10}"
LOCK="${KOAN_UF_LOCK:-$M/tmp/e2e.lock}"
ALL=(uf_build uf_onboarding uf_agent_task uf_indicator uf_resume uf_shots uf_render uf_toprow uf_compact uf_autofill)

usage() { sed -n '2,/^set -uo/p' "$0" | sed 's/^# \{0,1\}//; /^set -uo/d'; exit 2; }
app="$M/koan/chromium/src/out/Koan/Koan.app" only="" out="" keep_raw=0 video=1 note="" build_app=""
args=("$@")
while [[ $# -gt 0 ]]; do
  case "$1" in
    --app) app="$2"; shift 2 ;;
    --only) only="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --keep-raw) keep_raw=1; shift ;;
    --no-video) video=0; shift ;;
    --note) note="$2"; shift 2 ;;
    --build-app) build_app="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "run_flows.sh: unknown argument $1" >&2; usage ;;
  esac
done
app="$(cd "$app" 2>/dev/null && pwd)" || { echo "run_flows.sh: no app at $app" >&2; exit 2; }
[[ -f "$app/Contents/Info.plist" ]] || { echo "run_flows.sh: $app is not an app" >&2; exit 2; }
flows=("${ALL[@]}")
if [[ -n "$only" ]]; then
  IFS=, read -r -a flows <<<"$only"
  for f in "${flows[@]}"; do
    [[ -f "$here/flows/$f.mjs" ]] || { echo "run_flows.sh: no flow $f (flows/$f.mjs)" >&2; exit 2; }
  done
fi

# The lock, for the whole run (its children inherit the descriptor).
if [[ -z "${UF_LOCKED:-}" ]]; then
  export UF_LOCKED=1
  exec python3 -I "$ROOT/scripts/mac/tools/flock_run.py" "$LOCK" -- "$0" ${args[@]+"${args[@]}"}
fi

out="${out:-$M/uf/$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
log() { echo "run_flows.sh: $*" | tee -a "$out/run.log" >&2; }
index() {
  UF_ORDER="$(IFS=,; echo "${flows[*]}")" "$NODE" "$here/uf_index.mjs" "$out" --app "$app" ${note:+--note "$note"} \
    >"$out/index.md" 2>>"$out/run.log" || log "uf_index.mjs failed"
  log "index: $out/index.md"
}

# --- The gates.
gate_why() {
  local t
  for t in ptr keypress2 inputok.sh usergate.sh grantok.sh frontpid.sh; do
    [[ -x "$TOOLS/$t" ]] || { echo "no $TOOLS/$t (scripts/mac/README.md, Tools)"; return; }
  done
  [[ -e "$TOOLS/screen-granted" ]] || { echo "the screen is not granted ($TOOLS/screen-granted)"; return; }
  "$TOOLS/grantok.sh" >/dev/null 2>&1 || { echo "the screen is not granted (grantok.sh)"; return; }
  local idle
  idle="$("$TOOLS/usergate.sh" 2>/dev/null || echo 0)"
  [[ "$idle" =~ ^[0-9]+$ ]] && (( idle >= 600 )) || { echo "the Mac was used ${idle} s ago (need 600 s)"; return; }
  echo ""
}
why="$(gate_why)"
if [[ -n "$why" ]]; then
  log "not running: $why"
  printf '# Koan user flows: %s\n\nNot run: %s\n' "$(basename "$out")" "$why" >"$out/index.md"
  exit 3
fi
for t in ffmpeg ffprobe; do
  command -v "$t" >/dev/null || { log "no $t (Homebrew's, /opt/homebrew/bin)"; exit 2; }
done
# Tools the flows build when the bench has none.
[[ -x "$TOOLS/hold" ]] || log "note: no $TOOLS/hold (only the drag flows need it)"

cat >"$out/local_state.json" <<'JSON'
{"koan": {"agent_control": {"enabled": true}}}
JSON
log "app $app; flows ${flows[*]}; out $out"

# Runs <cmd...> with output to <log>, at most <secs>; 124 on a timeout.
descendants() {
  local c
  for c in $(pgrep -P "$1" 2>/dev/null); do echo "$c"; descendants "$c"; done
}
run_timeout() {  # <secs> <log> <cmd...>
  local secs="$1" logf="$2" p waited=0
  shift 2
  "$@" >"$logf" 2>&1 &
  p=$!
  while kill -0 "$p" 2>/dev/null; do
    if (( waited >= secs )); then
      echo "run_flows.sh: timed out after ${secs}s" >>"$logf"
      local d
      for d in $(descendants "$p") "$p"; do kill -TERM "$d" 2>/dev/null; done
      sleep 2
      for d in $(descendants "$p") "$p"; do kill -KILL "$d" 2>/dev/null; done
      wait "$p" 2>/dev/null
      return 124
    fi
    sleep 1
    waited=$((waited + 1))
  done
  wait "$p"
}

failed=0 stopped=0
for name in "${flows[@]}"; do
  fdir="$out/$name"
  rm -rf "$fdir"
  mkdir -p "$fdir/home" "$fdir/setup-home"
  why="$(gate_why)"
  if [[ -n "$why" ]]; then
    log "stopping before $name: $why"
    stopped=1
    break
  fi
  # What each flow needs from its Koan.
  launch_env=(KOAN_LOCAL_STATE="$out/local_state.json" KOAN_AGENT_SETUP_HOME="$fdir/setup-home")
  case "$name" in
    uf_toprow)
      launch_env+=(KOAN_EXTRA_EXTENSIONS="$ROOT/scripts/e2e/extensions/action_popup"
                   KOAN_PREFS="$ROOT/scripts/e2e/prefs/sidebar_top_row.json") ;;
    # chrome://components lists Widevine only with the component updater on.
    uf_build) launch_env+=(KOAN_COMPONENT_UPDATE=1) ;;
  esac
  log "--- $name"
  if ! env "${launch_env[@]}" "$here/launch.sh" "$app" >"$fdir/launch.out" 2>&1; then
    log "$name: launch.sh failed: $(tail -n 3 "$fdir/launch.out" | tr '\n' ' ')"
    "$NODE" "$here/uf_post.mjs" missing "$fdir" "$name" launch
    failed=1
    continue
  fi
  # (No `. <(...)`: the Mac's bash 3.2 can't source a process substitution.)
  while IFS='=' read -r key value; do
    export "$key=$value"
  done < <(grep -E '^[A-Z_]+=' "$fdir/launch.out")
  cp "$REC_DIR/koan.env" "$fdir/koan.env" 2>/dev/null
  vid="$fdir/video.mp4"
  recording=0
  if [[ $video == 1 ]]; then
    if "$here/record.sh" start "$vid" --cdp "$CDP_PORT" --max $((FLOW_SECS + 60)) >>"$fdir/record.log" 2>&1; then
      recording=1
    else
      log "$name: record.sh refused: $(tail -n 1 "$fdir/record.log")"
    fi
  fi
  flow_env=(MARKS="$fdir/marks.json" UF_OUT="$fdir" KOAN_CLI="$app/Contents/Resources/bin/koan" KOAN_APP="$app"
            KOAN_AGENT_HOME="$fdir/home" KOAN_AGENT_SETUP_HOME="$fdir/setup-home" KOAN_ROOT="$ROOT")
  [[ $keep_raw == 1 ]] && flow_env+=(UF_KEEP_RAW=1)
  [[ -n $build_app ]] && flow_env+=(KOAN_BUILD_APP="$build_app")
  run_timeout "$FLOW_SECS" "$fdir/flow.log" env "${flow_env[@]}" "$NODE" "$here/flows/$name.mjs"
  status=$?
  [[ $recording == 1 ]] && "$here/record.sh" stop "$vid" >>"$fdir/record.log" 2>&1
  "$here/quit.sh" "$KOAN_PID" >>"$fdir/record.log" 2>&1
  cp "$KOAN_LOG" "$fdir/koan.log" 2>/dev/null
  "$NODE" "$here/uf_post.mjs" missing "$fdir" "$name" "$status"
  [[ $status == 124 ]] && log "$name: timed out after ${FLOW_SECS}s"
  if grep -q '^STOPPED: ' "$fdir/flow.log"; then
    log "$name: $(grep -m1 '^STOPPED: ' "$fdir/flow.log")"
    case "$(grep -m1 '^STOPPED: ' "$fdir/flow.log")" in
      *"user is back"*|*"was used"*|*"real input stopped"*) stopped=1 ;;
    esac
  fi
  # The checks on the recording, then sheets and the small video.
  if [[ $recording == 1 && -s "$vid" ]]; then
    [[ "$name" == uf_compact ]] && "$NODE" "$here/uf_post.mjs" edge "$fdir" "$vid" >>"$fdir/post.log" 2>&1
    grep -q '"still"' "$fdir/marks.json" 2>/dev/null &&
      "$NODE" "$here/uf_post.mjs" still "$fdir" "$vid" >>"$fdir/post.log" 2>&1
    if "$here/frames.sh" "$vid" "$fdir/sheets" --marks "$fdir/marks.json" --before 0.3 --after 1.3 \
         --fps "$SHEET_FPS" >>"$fdir/post.log" 2>&1; then
      find "$fdir/sheets" -name 'sheet_*.png' -exec "$here/compress.sh" --sheet --replace {} + \
        >>"$fdir/post.log" 2>&1 || log "$name: compressing the sheets failed (post.log)"
    else
      log "$name: frames.sh failed (post.log)"
    fi
    line="$("$here/compress.sh" --check -o "$fdir/video.720.mp4" "$vid" 2>>"$fdir/post.log" | grep '^check' | head -1)"
    "$NODE" "$here/uf_post.mjs" video "$fdir" "$line" >>"$fdir/post.log" 2>&1
    [[ $keep_raw == 1 ]] || rm -f "$vid"
  fi
  [[ $keep_raw == 1 || "$REC_DIR" != "$M"/rec-* ]] || rm -rf "$REC_DIR/profile"
  result="$("$NODE" -e 'const s = JSON.parse(require("fs").readFileSync(process.argv[1])); console.log(s.status)' \
            "$fdir/summary.json" 2>/dev/null || echo fail)"
  log "$name: $result"
  [[ "$result" == pass || "$result" == skip ]] || failed=1
  # A modifier key left held makes every later press a modified one.
  mods="$("$TOOLS/ptr" mods 2>/dev/null || true)"
  [[ $mods == mods* && $mods != *held=none* ]] && log "after $name the Mac holds a modifier key ($mods)"
  if [[ $stopped == 1 ]]; then
    log "stopping the run: someone is at the Mac"
    break
  fi
done
index
if [[ $stopped == 1 ]]; then exit 3; fi
[[ $failed == 0 ]]
