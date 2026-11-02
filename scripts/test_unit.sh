#!/usr/bin/env bash
# Builds and runs Koan's unit tests (koan_unittests, src/test/BUILD.gn).
#
#   scripts/test_unit.sh [gtest args]       e.g. --gtest_filter='KoanFuzzy*'
#
# They cover Koan's pure logic and link without the browser, so the build is
# short and the run takes seconds. agent-build.sh runs this after every build.
# On macOS, source the folder's env.sh first (see build.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"$ROOT/scripts/build.sh" koan_unittests
cd "$ROOT/chromium/src"
exec out/Koan/koan_unittests "$@"
