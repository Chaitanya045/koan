#!/usr/bin/env bash
# shot_ui.sh <out.png> [extra chrome flags...] -- <urls...>
# Renders the full browser window (UI included) with the headless Ozone
# platform and copies the last dumped frame to <out.png>.
# Nothing appears on the real desktop. WAIT=<s> changes the render time.
out=$1; shift; flags=(); while [[ $# -gt 0 && $1 != -- ]]; do flags+=("$1"); shift; done; shift
prof=$(mktemp -d /tmp/koan-prof-XXXX); dump=$(mktemp -d /tmp/koan-dump-XXXX)
"$(dirname "$0")/../chromium/src/out/Koan/chrome" --ozone-platform=headless --ozone-dump-file="$dump" \
  --in-process-gpu --disable-gpu --user-data-dir="$prof" --no-first-run --window-size=1280,800 \
  "${flags[@]}" "$@" >"$prof.log" 2>&1 &
pid=$!
sleep "${WAIT:-8}"
kill $pid; wait $pid 2>/dev/null
f=$(ls -S "$dump"/*.png 2>/dev/null | head -1)
[[ -n $f ]] && cp "$f" "$out" && echo "wrote $out from $(ls "$dump")" || { echo "no frames"; tail -5 "$prof.log"; }
rm -rf "$prof" "$dump"
