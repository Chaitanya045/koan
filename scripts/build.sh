#!/usr/bin/env bash
# Configure out/Koan from build/koan.gn and build the browser.
# JOBS defaults to 5 so the build fits in 16 GB of RAM.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="$ROOT/depot_tools:$PATH"
JOBS="${JOBS:-5}"
TARGET="${1:-chrome}"

cd "$ROOT/chromium/src"
mkdir -p out/Koan
cp "$ROOT/build/koan.gn" out/Koan/args.gn
gn gen out/Koan
autoninja -C out/Koan -j "$JOBS" "$TARGET"
