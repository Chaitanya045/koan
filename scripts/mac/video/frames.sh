#!/bin/bash
# frames.sh: contact sheets from a record.sh video, for a model to look at.
#
#   frames.sh <video.mp4> <out dir> --marks <marks.json> [--before 1] [--after 3]
#   frames.sh <video.mp4> <out dir> --from <s> --to <s>
#   options: [--fps 30] [--tile 4x4] [--width 1500] [--crop sidebar|x,y,w,h]
#            [--full] [--frames]
#
# Each time range (with --marks: [mark - before, mark + after] around every
# action mark of a flow's log, merged where they overlap; the video's
# <video>.rec.json maps the marks' wall clock to video time) is cut into
# frames at --fps (30 by default: every frame, so a one-frame flash is in
# there), and every 16 frames make one 4x4 contact sheet (ffmpeg's tile
# filter), at most --width px wide and high, each frame stamped with its
# video time and the round and seconds since the latest action:
# "12.345s r03 +0.43". Frames are decoded at one pixel per point (--full: at
# the recording's device pixels); --crop cuts the sidebar (its rect is in the
# marks) or a rect in points inside the recorded window first, which makes
# the rows far easier to read.
#
# Out, per range, in <out dir>/<from>-<label>/:
#   sheet_NNN.png  the contact sheets
#   diff.tsv       per frame: n, video time, wall clock (epoch ms), changed
#                  pixels since the previous frame, changed %, their bbox
#                  (x,y,w,h in the decoded frame)
#   sheets.tsv     per sheet: path, first and last frame time, frames, most
#                  pixels changed in one frame, frames with >= MIN_CHANGED_PX
#                  (1200) changed, the action marks inside
#   frames/        with --frames: every frame as a JPEG (n as in diff.tsv)
# and <out dir>/sheets.tsv (all of them) and <out dir>/changed.tsv (only the
# sheets where something changed: the ones to look at).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
FFMPEG="${FFMPEG:-$(command -v ffmpeg || echo /opt/homebrew/bin/ffmpeg)}"
FFPROBE="${FFPROBE:-$(command -v ffprobe || echo /opt/homebrew/bin/ffprobe)}"
NODE="${NODE:-$(command -v node || echo /opt/homebrew/bin/node)}"
die() { echo "frames.sh: $*" >&2; exit 1; }

video="${1:-}"
out="${2:-}"
[[ -f "$video" && -n "$out" ]] || die "usage: frames.sh <video.mp4> <out dir> (--marks <json> | --from s --to s) [options]"
shift 2
marks="" from="" to="" before=1 after=3 fps=30 tile=4x4 width=1500 crop="-" full=0 keep=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --marks) marks="$2"; shift 2 ;;
    --from) from="$2"; shift 2 ;;
    --to) to="$2"; shift 2 ;;
    --before) before="$2"; shift 2 ;;
    --after) after="$2"; shift 2 ;;
    --fps) fps="$2"; shift 2 ;;
    --tile) tile="$2"; shift 2 ;;
    --width) width="$2"; shift 2 ;;
    --crop) crop="$2"; shift 2 ;;
    --full) full=1; shift ;;
    --frames) keep=1; shift ;;
    *) die "unknown option $1" ;;
  esac
done
rec="$video.rec.json"
[[ -f "$rec" ]] || rec="-"
cols="${tile%x*}" rows="${tile#*x}"
[[ "$cols" =~ ^[0-9]+$ && "$rows" =~ ^[0-9]+$ ]] || die "bad --tile $tile"
per=$(( cols * rows ))
# Each tile's box, so the sheet stays within --width both ways.
tw=$(( (width - 8 - (cols - 1) * 4) / cols ))
th=$(( (width - 8 - (rows - 1) * 4) / rows ))
scale=2
[[ "$rec" != "-" ]] && scale="$("$NODE" -e 'console.log(JSON.parse(require("fs").readFileSync(process.argv[1])).scale ?? 2)' "$rec")"
div=$(( full ? 1 : scale ))

# The ranges: "from<TAB>to<TAB>label<TAB>crop (video px x:y:w:h, or -)".
if [[ -n "$marks" ]]; then
  [[ "$rec" != "-" ]] || die "--marks needs $video.rec.json (from record.sh) to map the marks to video time"
  ranges="$("$NODE" "$here/sheet.mjs" plan "$marks" "$rec" "$before" "$after" "$crop")"
else
  [[ -n "$from" && -n "$to" ]] || die "give --marks, or --from and --to"
  [[ "$crop" == sidebar ]] && die "--crop sidebar needs --marks"
  pxcrop="-"
  if [[ "$crop" != "-" ]]; then
    IFS=, read -r cx cy cw ch <<<"$crop"
    pxcrop="$(( cx * scale / 2 * 2 )):$(( cy * scale / 2 * 2 )):$(( cw * scale / 2 * 2 )):$(( ch * scale / 2 * 2 ))"
  fi
  ranges="$(printf '%s\t%s\t%s\t%s\n' "$from" "$to" range "$pxcrop")"
fi
[[ -n "$ranges" ]] || die "no ranges (no action marks inside the video?)"

read -r vw vh < <("$FFPROBE" -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x "$video" | tr x " ")
mkdir -p "$out"
printf 'sheet\tfrom\tto\tframes\tmax_changed_px\tchanged_frames\tmarks\n' >"$out/sheets.tsv"
while IFS=$'\t' read -r rfrom rto label pcrop; do
  dir="$out/$(printf '%08.3f' "$rfrom")-$(tr -c 'A-Za-z0-9+_-' '_' <<<"$label" | sed 's/_*$//')"
  mkdir -p "$dir"
  rm -f "$dir"/sheet_*.png "$dir/diff.tsv" "$dir/sheets.tsv"
  printf 'n\tt\twall_ms\tchanged_px\tchanged_pct\tbbox\n' >"$dir/diff.tsv"
  filt="fps=$fps"
  if [[ "$pcrop" != "-" ]]; then
    IFS=: read -r cx cy cw ch <<<"$pcrop"
    filt+=",crop=$cw:$ch:$cx:$cy"
  else
    cw=$vw ch=$vh
  fi
  w=$(( cw / div / 2 * 2 )) h=$(( ch / div / 2 * 2 ))
  filt+=",scale=$w:$h:flags=area"
  "$FFMPEG" -nostdin -v error -ss "$rfrom" -to "$rto" -i "$video" -vf "$filt" -f rawvideo -pix_fmt rgb24 pipe:1 |
    "$NODE" "$here/sheet.mjs" stamp "$w" "$h" "$fps" "$rfrom" "$per" "$dir/" "$rec" "${marks:--}" |
    "$FFMPEG" -nostdin -v error -f rawvideo -pix_fmt rgb24 -s "${w}x${h}" -r "$fps" -i pipe:0 \
      -vf "scale=w=$tw:h=$th:force_original_aspect_ratio=decrease:force_divisible_by=2:flags=area,tile=${cols}x${rows}:margin=4:padding=4:color=0x303030" \
      -start_number 0 "$dir/sheet_%03d.png"
  if (( keep )); then
    mkdir -p "$dir/frames"
    "$FFMPEG" -nostdin -v error -ss "$rfrom" -to "$rto" -i "$video" -vf "$filt" -q:v 3 -start_number 0 "$dir/frames/f_%05d.jpg"
  fi
  cat "$dir/sheets.tsv" >>"$out/sheets.tsv"
  echo "frames.sh: $label  ${rfrom}s..${rto}s  -> $dir ($(wc -l <"$dir/sheets.tsv" | tr -d ' ') sheet(s))"
done <<<"$ranges"
# The sheets worth a look: at least one frame with MIN_CHANGED_PX changed.
awk -F'\t' 'NR == 1 || $6 > 0' "$out/sheets.tsv" >"$out/changed.tsv"
echo "frames.sh: $(( $(wc -l <"$out/sheets.tsv") - 1 )) sheet(s), $(( $(wc -l <"$out/changed.tsv") - 1 )) with changes: $out/changed.tsv"
