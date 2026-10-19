#!/usr/bin/env bash
# run_profile.sh <profile-name> <ui-devtools-port> <cdp-port> [flags...] -- <urls...>
# Runs Koan off-screen on a *persistent* test profile
# ($KOAN_TEST_DIR/<profile-name>, kept between runs) with UI DevTools and the
# DevTools protocol enabled, until it is closed (e.g. scripts/cdp_close.mjs).
# Frames go to $KOAN_TEST_DIR/<profile-name>.dump/. Use it to test restarts.
source "$(dirname "$0")/koan_test_env.sh"
name=$1; uiport=$2; cdpport=$3; shift 3
flags=(); while [[ $# -gt 0 && $1 != -- ]]; do flags+=("$1"); shift; done; shift
prof="$KOAN_TEST_DIR/$name"; dump="$KOAN_TEST_DIR/$name.dump"
mkdir -p "$prof" "$dump"
exec "$KOAN_CHROME" "${KOAN_HEADLESS_UI_FLAGS[@]}" --ozone-dump-file="$dump" \
  --user-data-dir="$prof" --enable-ui-devtools="$uiport" \
  --enable-features=ui-debug-tools-enable-synthetic-events \
  --remote-debugging-port="$cdpport" "${flags[@]}" "$@" >"$prof.log" 2>&1
