#!/bin/bash
# compress.sh: makes screenshots, recordings and contact sheets small enough
# to review cheaply (a model pays per pixel), without losing what a review
# looks for: text, 1-2 px edges, one-frame glitches.
#
#   compress.sh [options] <file>...
#
# By kind (from the extension):
#   still  (.png .jpg .jpeg)  fit within 1920x1080 (--720: 1280x720, for
#                             overview shots only), never enlarged; PNG kept
#                             (UI with text), or JPEG at quality 90 with
#                             --jpeg (4:4:4 chroma, so coloured text keeps
#                             its edges)
#   video  (.mp4 .mov .m4v .mkv)  H.264 within 1280x720 (--1080: 1920x1080),
#                             never enlarged, the frame rate kept as it is
#                             (record.sh's 30 fps: frame-level glitch checks
#                             still work), no audio, faststart; the hardware
#                             encoder (h264_videotoolbox) when ffmpeg has it,
#                             else libx264 at CRF 23, -preset medium
#   sheet  (--sheet, a .png)  a frames.sh contact sheet: fit to 1280 wide
#
# The box is orientation-aware: a portrait image fits 1080x1920. Nothing is
# ever made smaller than the 720p box, and nothing is enlarged. A video's
# <file>.rec.json (record.sh) is copied next to the output with its scale
# (device pixels per point) adjusted, so edge_check.mjs still maps marks
# onto the smaller video. frames.sh wants a whole-number scale: cut sheets
# from the original (run_flows.sh does), then compress them with --sheet.
#
# Options:
#   -o, --out FILE      the output (one input only); default <stem>.<tag>.<ext>
#                       next to the input (tag 1080, 720 or sheet)
#   --out-dir DIR       outputs go there, named as above
#   --720 | --1080      the box (stills default 1080, videos 720)
#   --jpeg              stills as JPEG (q90)
#   --sheet             contact sheets (fit 1280 wide)
#   --check             after each file: sizes before and after, dimensions,
#                       and SSIM against the original (the original scaled to
#                       the output's size the same way, so it measures the
#                       encoding; "res" is the output scaled back up against
#                       the original, for information). WARN when SSIM is
#                       below 0.97 for stills or 0.95 for videos, and when a
#                       still was made more than 2x smaller (1 px lines may
#                       go). Videos are compared over their first
#                       $KOAN_CHECK_SECS (30) s.
#   --strict            with --check, exit 4 if any file warned
#   --encoder auto|videotoolbox|libx264
#   --plan              print what would be done, one tab-separated line per
#                       file: plan, input, kind, WxH, ->, WxH, codec, output;
#                       runs no encoder
#   --in-size WxH       with --plan: the input's size (no ffprobe; tests)
#   --replace           replace the input with the output (stills only)
#
# Exit: 0 done, 2 bad arguments, 3 no ffmpeg/ffprobe, 4 a --strict warning,
# 5 an encoder failed.
set -euo pipefail
FFMPEG="${FFMPEG:-$(command -v ffmpeg || echo /opt/homebrew/bin/ffmpeg)}"
FFPROBE="${FFPROBE:-$(command -v ffprobe || echo /opt/homebrew/bin/ffprobe)}"
CHECK_SECS="${KOAN_CHECK_SECS:-30}"
STILL_MIN_SSIM=0.97
VIDEO_MIN_SSIM=0.95

usage() { sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//; /^set -euo/d'; exit 2; }
die() { echo "compress.sh: $*" >&2; exit "${code:-2}"; }

out="" out_dir="" box="" jpeg=0 sheet=0 check=0 strict=0 encoder=auto plan=0 in_size="" replace=0
files=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    -o|--out) [[ $# -ge 2 ]] || die "$1 needs a file"; out="$2"; shift 2 ;;
    --out-dir) [[ $# -ge 2 ]] || die "$1 needs a directory"; out_dir="$2"; shift 2 ;;
    --720|--1080)
      [[ -z "$box" || "$box" == "${1#--}" ]] || die "give --720 or --1080, not both"
      box="${1#--}"; shift ;;
    --jpeg) jpeg=1; shift ;;
    --sheet) sheet=1; shift ;;
    --check) check=1; shift ;;
    --strict) strict=1; shift ;;
    --encoder)
      [[ $# -ge 2 ]] || die "--encoder needs auto, videotoolbox or libx264"
      case "$2" in auto|videotoolbox|libx264) encoder="$2" ;; *) die "unknown encoder $2" ;; esac
      shift 2 ;;
    --plan) plan=1; shift ;;
    --in-size)
      [[ $# -ge 2 && "$2" =~ ^[0-9]+x[0-9]+$ ]] || die "--in-size wants WxH"
      in_size="$2"; shift 2 ;;
    --replace) replace=1; shift ;;
    -h|--help) usage ;;
    --) shift; files+=("$@"); break ;;
    -*) die "unknown option $1 (compress.sh --help)" ;;
    *) files+=("$1"); shift ;;
  esac
done
[[ ${#files[@]} -gt 0 ]] || die "no input files (compress.sh --help)"
[[ -z "$out" || ${#files[@]} -eq 1 ]] || die "-o names one output: give one input, or --out-dir"
[[ -z "$out" || -z "$out_dir" ]] || die "give -o or --out-dir, not both"
[[ -z "$in_size" || $plan == 1 ]] || die "--in-size is for --plan only"
[[ $strict == 0 || $check == 1 ]] || die "--strict needs --check"
[[ $sheet == 0 || -z "$box" ]] || die "--sheet has its own size (1280 wide): no --720/--1080"
[[ $replace == 0 || ( -z "$out" && -z "$out_dir" ) ]] || die "--replace writes over the input: no -o/--out-dir"

kind_of() {  # <file>
  local ext
  ext="$(tr '[:upper:]' '[:lower:]' <<<"${1##*.}")"
  case "$ext" in
    png) if [[ $sheet == 1 ]]; then echo sheet; else echo still; fi ;;
    jpg|jpeg) [[ $sheet == 0 ]] || die "--sheet takes frames.sh's .png sheets: $1"; echo still ;;
    mp4|mov|m4v|mkv) [[ $sheet == 0 ]] || die "--sheet takes .png sheets, not a video: $1"; echo video ;;
    *) die "don't know what $1 is (png, jpg, mp4, mov, m4v, mkv)" ;;
  esac
}

# fit <w> <h> <kind> -> "W H": the size within the kind's box, never larger
# than the input; even for video (H.264 4:2:0).
fit() {
  local w="$1" h="$2" kind="$3" bw bh even=0
  case "$kind" in
    still) if [[ "$box" == 720 ]]; then bw=1280 bh=720; else bw=1920 bh=1080; fi ;;
    video) if [[ "$box" == 1080 ]]; then bw=1920 bh=1080; else bw=1280 bh=720; fi; even=1 ;;
    sheet) bw=1280 bh=0 ;;
  esac
  awk -v w="$w" -v h="$h" -v bw="$bw" -v bh="$bh" -v even="$even" 'BEGIN {
    if (bh > 0 && h > w) { t = bw; bw = bh; bh = t }   # portrait: the box turns too
    s = 1
    if (bw / w < s) s = bw / w
    if (bh > 0 && bh / h < s) s = bh / h
    W = int(w * s + 0.5); H = int(h * s + 0.5)
    if (W > bw) W = bw
    if (bh > 0 && H > bh) H = bh
    if (even) { W = int(W / 2) * 2; H = int(H / 2) * 2 }
    if (W < 1) W = 1
    if (H < 1) H = 1
    if (even && W < 2) W = 2
    if (even && H < 2) H = 2
    printf "%d %d\n", W, H
  }'
}

size_of() {  # <file> -> "w h"
  if [[ -n "$in_size" ]]; then
    tr x ' ' <<<"$in_size"
    return
  fi
  [[ -x "$FFPROBE" || -n "$(command -v "$FFPROBE" 2>/dev/null)" ]] || code=3 die "no ffprobe ($FFPROBE)"
  local s
  s="$("$FFPROBE" -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x "$1" | head -1)"
  [[ "$s" =~ ^[0-9]+x[0-9]+$ ]] || code=5 die "ffprobe could not read the size of $1 ('$s')"
  tr x ' ' <<<"$s"
}

has_videotoolbox() {
  "$FFMPEG" -hide_banner -encoders 2>/dev/null | grep -q '\bh264_videotoolbox\b'
}

# The output's name and codec.
codec_of() {  # <kind>
  case "$1" in
    still) if [[ $jpeg == 1 ]]; then echo jpeg; else echo png; fi ;;
    sheet) echo png ;;
    video)
      case "$encoder" in
        libx264) echo libx264 ;;
        videotoolbox) echo h264_videotoolbox ;;
        auto) if [[ $plan == 1 && -n "$in_size" ]]; then echo h264-auto
              elif has_videotoolbox; then echo h264_videotoolbox
              else echo libx264; fi ;;
      esac ;;
  esac
}
out_of() {  # <in> <kind> <codec>
  local in="$1" kind="$2" codec="$3" stem dir tag ext
  if [[ -n "$out" ]]; then echo "$out"; return; fi
  dir="$(dirname "$in")"
  [[ -n "$out_dir" ]] && dir="$out_dir"
  stem="$(basename "${in%.*}")"
  case "$kind" in
    still) tag="${box:-1080}" ;;
    video) tag="${box:-720}" ;;
    sheet) tag=sheet ;;
  esac
  case "$codec" in jpeg) ext=jpg ;; png) ext=png ;; *) ext=mp4 ;; esac
  if [[ $replace == 1 ]]; then
    [[ "$ext" == "$(tr '[:upper:]' '[:lower:]' <<<"${in##*.}")" ]] ||
      die "--replace keeps the format: $in would become .$ext"
    echo "$in"
    return
  fi
  echo "$dir/$stem.$tag.$ext"
}

ssim_of() {  # <a> <b> <W> <H> <secs|''>: SSIM of a against b scaled to WxH
  local a="$1" b="$2" W="$3" H="$4" secs="$5" t=()
  [[ -n "$secs" ]] && t=(-t "$secs")
  "$FFMPEG" -nostdin -hide_banner -v info ${t[@]+"${t[@]}"} -i "$a" ${t[@]+"${t[@]}"} -i "$b" -lavfi \
    "[0:v]scale=$W:$H:flags=lanczos,format=yuv444p[a];[1:v]scale=$W:$H:flags=lanczos,format=yuv444p[b];[a][b]ssim" \
    -f null - 2>&1 | sed -n 's/.*SSIM .*All:\([0-9.]*\).*/\1/p' | tail -1
}
bytes_of() { wc -c <"$1" | tr -d ' '; }

warned=0
for in in "${files[@]}"; do
  [[ -f "$in" ]] || die "no such file: $in"
  kind="$(kind_of "$in")"
  [[ $jpeg == 0 || "$kind" == still ]] || die "--jpeg is for stills: $in"
  [[ $replace == 0 || "$kind" != video ]] || die "--replace is for stills: $in"
  wh="$(size_of "$in")"
  read -r w h <<<"$wh"
  WH="$(fit "$w" "$h" "$kind")"
  read -r W H <<<"$WH"
  codec="$(codec_of "$kind")"
  dst="$(out_of "$in" "$kind" "$codec")"
  if [[ $replace == 0 && "$(cd "$(dirname "$dst")" 2>/dev/null && pwd)/$(basename "$dst")" == \
        "$(cd "$(dirname "$in")" && pwd)/$(basename "$in")" ]]; then
    die "the output would overwrite the input ($in); use --replace (stills) or -o"
  fi
  if [[ $plan == 1 ]]; then
    printf 'plan\t%s\t%s\t%sx%s\t->\t%sx%s\t%s\t%s\n' "$in" "$kind" "$w" "$h" "$W" "$H" "$codec" "$dst"
    continue
  fi
  [[ -x "$FFMPEG" || -n "$(command -v "$FFMPEG" 2>/dev/null)" ]] || code=3 die "no ffmpeg ($FFMPEG)"
  mkdir -p "$(dirname "$dst")"
  tmp="$dst.tmp.$$.${dst##*.}"
  rm -f "$tmp"
  scale="scale=$W:$H:flags=lanczos"
  case "$kind/$codec" in
    still/png|sheet/png)
      if [[ "$W" == "$w" && "$H" == "$h" && "$(tr '[:upper:]' '[:lower:]' <<<"${in##*.}")" == png ]]; then
        cp "$in" "$tmp"
      else
        "$FFMPEG" -nostdin -v error -y -i "$in" -vf "$scale" -frames:v 1 -update 1 -c:v png "$tmp" ||
          code=5 die "ffmpeg failed on $in"
      fi ;;
    still/jpeg)
      "$FFMPEG" -nostdin -v error -y -i "$in" -vf "$scale,format=yuvj444p" -frames:v 1 -update 1 \
        -c:v mjpeg -q:v 2 "$tmp" || code=5 die "ffmpeg failed on $in" ;;
    video/*)
      common=(-nostdin -v error -y -i "$in" -an -vf "$scale,format=yuv420p" -fps_mode passthrough
              -movflags +faststart)
      ok=0
      if [[ "$codec" == h264_videotoolbox ]]; then
        # No CRF in the hardware encoder: a bitrate for screen content
        # (0.15 bit a pixel a frame: 720p30 ~4 Mb/s, 1080p30 ~9 Mb/s). --check
        # measures what it gave.
        fps="$("$FFPROBE" -v error -select_streams v:0 -show_entries stream=avg_frame_rate -of csv=p=0 "$in" |
               awk -F/ '{ if ($2 > 0) print $1 / $2; else print 30 }')"
        rate="$(awk -v w="$W" -v h="$H" -v f="$fps" 'BEGIN { printf "%d", w * h * f * 0.15 }')"
        if "$FFMPEG" "${common[@]}" -c:v h264_videotoolbox -b:v "$rate" -maxrate "$((rate * 2))" \
             -bufsize "$((rate * 2))" -profile:v high "$tmp"; then
          ok=1
        else
          echo "compress.sh: h264_videotoolbox failed on $in; libx264 instead" >&2
          codec=libx264
        fi
      fi
      if [[ $ok == 0 ]]; then
        "$FFMPEG" "${common[@]}" -c:v libx264 -preset medium -crf 23 -profile:v high "$tmp" ||
          code=5 die "ffmpeg (libx264) failed on $in"
      fi ;;
  esac
  mv -f "$tmp" "$dst"
  if [[ "$kind" == video && -f "$in.rec.json" ]]; then
    python3 -I - "$in.rec.json" "$dst.rec.json" "$w" "$W" "$dst" <<'PY'
import json, sys
src, dst, w, W, out = sys.argv[1:6]
rec = json.load(open(src))
rec['compressedFrom'] = rec.get('file')
rec['file'] = out
rec['scale'] = rec.get('scale', 2) * int(W) / int(w)
json.dump(rec, open(dst, 'w'))
PY
  fi
  line="compress.sh: $in ($kind ${w}x${h}) -> $dst (${W}x${H}, $codec)"
  if [[ $check == 1 ]]; then
    secs=""
    min=$STILL_MIN_SSIM
    if [[ "$kind" == video ]]; then secs="$CHECK_SECS"; min=$VIDEO_MIN_SSIM; fi
    enc="$(ssim_of "$dst" "$in" "$W" "$H" "$secs")"
    res="$(ssim_of "$dst" "$in" "$w" "$h" "$secs")"
    before="$(bytes_of "$in")" after="$(bytes_of "$dst")"
    verdict=ok why=""
    if [[ -z "$enc" ]] || awk -v s="$enc" -v m="$min" 'BEGIN { exit !(s < m) }'; then
      verdict=WARN why="SSIM ${enc:-unknown} < $min"
    fi
    # (A still only: a 720p video of a Retina window is under 1 px a point
    # by design; frames.sh and edge_check.mjs read the original.)
    if [[ "$kind" != video ]] && awk -v w="$w" -v W="$W" 'BEGIN { exit !(W / w < 0.5) }'; then
      verdict=WARN why="${why:+$why; }more than 2x smaller (1 px lines may go)"
    fi
    [[ "$verdict" == WARN ]] && warned=1
    printf 'check\t%s\t%s\t%s\t%sx%s\t%sx%s\tssim=%s\tres=%s\t%s%s\n' "$dst" "$before" "$after" \
      "$w" "$h" "$W" "$H" "${enc:-?}" "${res:-?}" "$verdict" "${why:+ ($why)}"
  fi
  echo "$line" >&2
done
if [[ $strict == 1 && $warned == 1 ]]; then exit 4; fi
exit 0
