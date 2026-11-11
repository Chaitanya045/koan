#!/bin/bash
# record.sh: a screen recording of one Koan window's region (ffmpeg
# avfoundation, 30 fps, H.264), for checking UI glitches frame by frame.
#
#   record.sh start <out.mp4> (--cdp <port> | --rect x,y,w,h) [--max <secs>]
#   record.sh stop  <out.mp4>
#
# start: refuses unless nobody has used the Mac for 600 s (usergate.sh) and
# the screen is granted ($KOAN_MAC_TOOLS/screen-granted). The region is the
# window's bounds in screen points: from CDP (--cdp: the biggest window of the
# Koan answering on that port, see window_rect.mjs) or given (--rect). It is
# cropped from the full screen capture at $REC_SCALE device pixels per point
# (2 on the bench's Retina screen). The encoder is the hardware one
# (h264_videotoolbox), so recording takes little CPU from the Koan under
# test; libx264 if that fails. ffmpeg stops by itself after --max seconds
# (default 900).
#
# Beside the video: <out>.rec.json, {file, rect, scale, fps, device, t0,
# pid}. t0 is the wall clock (epoch ms) of the video's time 0, so a flow's
# marks (Date.now()) map to video times: (mark - t0) / 1000. It is measured
# from ffmpeg's -progress output (the earliest "now - out_time" over the first
# seconds), so it is late by at most the encoder's delay (~0.1 s).
#
# stop: SIGTERM to that ffmpeg (it finishes the MP4; a background job of a
# script ignores SIGINT), waits for it, prints the
# video's duration.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
M="${KOAN_MAC_ROOT:-$HOME/work/koan-mac}"
TOOLS="${KOAN_MAC_TOOLS:-$M/tmp}"
FFMPEG="${FFMPEG:-/opt/homebrew/bin/ffmpeg}"
FFPROBE="${FFPROBE:-/opt/homebrew/bin/ffprobe}"
NODE="${NODE:-$(command -v node || echo /opt/homebrew/bin/node)}"
SCALE="${REC_SCALE:-2}"
FPS=30
MIN_IDLE=600

die() { echo "record.sh: $*" >&2; exit "${code:-1}"; }
now_ms() { perl -MTime::HiRes=time -e 'printf "%d\n", time * 1000'; }

cmd="${1:-}"
out="${2:-}"
[[ -n "$cmd" && -n "$out" ]] || die "usage: record.sh start <out.mp4> (--cdp <port> | --rect x,y,w,h) [--max s] | stop <out.mp4>"
shift 2
meta="$out.rec.json"
pidfile="$out.pid"
progress="$out.progress"

if [[ "$cmd" == stop ]]; then
  [[ -f "$pidfile" ]] || die "no recording running for $out (no $pidfile)"
  pid="$(cat "$pidfile")"
  # Only our ffmpeg: its command line names this output.
  if ps -o command= -p "$pid" 2>/dev/null | grep -qF -- "$out"; then
    kill -TERM "$pid"
    for _ in $(seq 1 60); do
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.5
    done
    kill -0 "$pid" 2>/dev/null && { kill -KILL "$pid"; echo "record.sh: ffmpeg did not finish in 30 s; killed (the MP4 may be broken)" >&2; }
  else
    echo "record.sh: ffmpeg $pid already gone"
  fi
  rm -f "$pidfile"
  dur="$("$FFPROBE" -v error -show_entries format=duration -of default=nw=1:nk=1 "$out" 2>/dev/null || true)"
  echo "record.sh: stopped; $out is ${dur:-?} s (log: $out.ffmpeg.log)"
  exit 0
fi
[[ "$cmd" == start ]] || die "unknown command $cmd"

rect="" max=900
while [[ $# -gt 0 ]]; do
  case "$1" in
    --rect) rect="$2"; shift 2 ;;
    --cdp) rect="$(CDP_PORT="$2" "$NODE" "$here/window_rect.mjs")" || die "no window rect from CDP port $2"; shift 2 ;;
    --max) max="$2"; shift 2 ;;
    *) die "unknown option $1" ;;
  esac
done
[[ "$rect" =~ ^-?[0-9]+,-?[0-9]+,[0-9]+,[0-9]+$ ]] || die "bad or missing rect '$rect' (want x,y,w,h in points)"
[[ -f "$pidfile" ]] && die "a recording for $out is already running (pid $(cat "$pidfile"))"

# The gates: the user away for 10 minutes, the screen granted.
idle="$("$TOOLS/usergate.sh" 2>/dev/null || echo 0)"
[[ "$idle" =~ ^[0-9]+$ ]] || idle=0
(( idle >= MIN_IDLE )) || code=5 die "refusing: the Mac was used ${idle} s ago (need >= $MIN_IDLE s idle)"
[[ -e "$TOOLS/screen-granted" ]] || code=6 die "refusing: the screen is not granted ($TOOLS/screen-granted missing)"

# The screen's avfoundation device ("Capture screen 0").
dev="$("$FFMPEG" -hide_banner -f avfoundation -list_devices true -i "" 2>&1 |
       sed -n 's/.*\[\([0-9]*\)\] Capture screen 0.*/\1/p' | head -1)"
[[ -n "$dev" ]] || die "no 'Capture screen 0' avfoundation device"

IFS=, read -r rx ry rw rh <<<"$rect"
# Device pixels, even (H.264 4:2:0), clamped at the screen's top-left.
px=$(( rx < 0 ? 0 : rx * SCALE )); py=$(( ry < 0 ? 0 : ry * SCALE ))
pw=$(( rw * SCALE / 2 * 2 )); ph=$(( rh * SCALE / 2 * 2 ))
crop="crop=${pw}:${ph}:${px}:${py}"

mkdir -p "$(dirname "$out")"
rm -f "$progress"
start_ffmpeg() {  # <encoder args...>
  "$FFMPEG" -nostdin -hide_banner -loglevel warning -y \
    -f avfoundation -capture_cursor 1 -capture_mouse_clicks 1 -framerate "$FPS" -i "${dev}:none" \
    -t "$max" -vf "${crop},format=yuv420p" -fps_mode cfr -r "$FPS" "$@" \
    -progress "$progress" -stats_period 0.25 "$out" >"$out.ffmpeg.log" 2>&1 &
  echo $!
}
started="$(now_ms)"
pid="$(start_ffmpeg -c:v h264_videotoolbox -b:v 16M -realtime 1 -g 30)"
sleep 3
if ! kill -0 "$pid" 2>/dev/null; then
  echo "record.sh: h264_videotoolbox failed, retrying with libx264:" >&2
  tail -n 5 "$out.ffmpeg.log" >&2
  rm -f "$progress"
  started="$(now_ms)"
  pid="$(start_ffmpeg -c:v libx264 -preset ultrafast -crf 18 -g 30)"
  sleep 3
  kill -0 "$pid" 2>/dev/null || { tail -n 20 "$out.ffmpeg.log" >&2; die "ffmpeg did not start"; }
fi
echo "$pid" >"$pidfile"

# t0: the earliest now - out_time over ~3 s of progress reports.
t0=""
for _ in $(seq 1 30); do
  us="$(grep '^out_time_us=' "$progress" 2>/dev/null | tail -1 | cut -d= -f2 || true)"
  if [[ "$us" =~ ^[0-9]+$ ]] && (( us > 0 )); then
    cand=$(( $(now_ms) - us / 1000 ))
    if [[ -z "$t0" ]] || (( cand < t0 )); then t0=$cand; fi
  fi
  sleep 0.1
done
if [[ -z "$t0" ]]; then
  kill -TERM "$pid" 2>/dev/null || true
  rm -f "$pidfile"
  tail -n 20 "$out.ffmpeg.log" >&2
  die "ffmpeg wrote no frames in 6 s (no screen-recording permission for this process?)"
fi
cat >"$meta" <<JSON
{"file": "$out", "rect": {"x": $rx, "y": $ry, "w": $rw, "h": $rh}, "scale": $SCALE, "fps": $FPS,
 "device": "$dev", "crop": "$crop", "startedAt": $started, "t0": $t0, "pid": $pid, "max": $max}
JSON
echo "record.sh: recording $rect (points) to $out, ffmpeg pid $pid, t0 $t0; stop with: record.sh stop $out"
