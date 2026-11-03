#!/usr/bin/env bash
# Builds and runs Koan's browser tests (koan_browsertests, src/test/BUILD.gn):
# Spaces, pinned tabs and the session data, in a real browser window.
#
#   scripts/test_browser.sh [gtest args]    e.g. --gtest_filter='*SessionRestore*'
#
# Slow: they build Chromium's browser test support and start a browser per
# test (the PRE_ tests start two). Run them when you touch Spaces, pinned
# tabs or the session data, not on every build. On Linux they run off-screen
# (headless Ozone), like the end-to-end tests; on macOS, source the folder's
# env.sh first (see build.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/scripts/build.sh" koan_browsertests
cd "$ROOT/chromium/src"
flags=()
if [[ $(uname -s) == Linux ]]; then
  flags=(--ozone-platform=headless --in-process-gpu --disable-gpu
    --password-store=basic "--window-size=1280,800"
    "--ozone-override-screen-size=1280,800")
fi
out=out/Koan
[[ ${KOAN_CONFIG:-} == release ]] && out=out/Release
exec "$out/koan_browsertests" "${flags[@]}" "$@"
