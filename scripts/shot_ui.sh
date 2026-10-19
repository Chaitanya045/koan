#!/usr/bin/env bash
# shot_ui.sh <out.png> [extra chrome flags...] -- <urls...>
# Renders the full browser window (UI included) with the headless Ozone
# platform and copies the last dumped frame to <out.png>.
# Nothing appears on the real desktop. WAIT=<s> changes the render time.
# PREFS=<json> seeds Default/Preferences (e.g. '{"koan":{"compact_mode":{"enabled":true}}}').
source "$(dirname "$0")/koan_test_env.sh"
out=$1; shift
flags=(); while [[ $# -gt 0 && $1 != -- ]]; do flags+=("$1"); shift; done; shift
prof=$(koan_new_profile); dump=$(mktemp -d "$KOAN_TEST_DIR/dump-XXXX")
pid=
cleanup() { [[ -n $pid ]] && kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; koan_rm "$prof" "$dump" "$prof.log"; }
trap cleanup EXIT
if [[ -n ${PREFS:-} ]]; then mkdir -p "$prof/Default"; printf '%s' "$PREFS" > "$prof/Default/Preferences"; fi
"$KOAN_CHROME" "${KOAN_HEADLESS_UI_FLAGS[@]}" --ozone-dump-file="$dump" \
  --user-data-dir="$prof" "${flags[@]}" "$@" >"$prof.log" 2>&1 &
pid=$!
sleep "${WAIT:-8}"
f=$(ls -S "$dump"/*.png 2>/dev/null | head -1)
if [[ -n $f ]]; then cp "$f" "$out" && echo "wrote $out"; else echo "no frames"; tail -5 "$prof.log"; fi
