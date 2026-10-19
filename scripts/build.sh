#!/usr/bin/env bash
# Configure and build Koan.
#
#   scripts/build.sh [target]                   development build (out/Koan)
#   KOAN_CONFIG=release scripts/build.sh [target]   release build (out/Release)
#
# Development: component build with DCHECKs, fast incremental rebuilds.
# Release: one optimised binary, no DCHECKs, what gets packaged and shipped.
# JOBS defaults to 4 so a build fits next to other apps in 16 GB of RAM.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="$ROOT/depot_tools:$PATH"
JOBS="${JOBS:-4}"
TARGET="${1:-chrome}"

case "${KOAN_CONFIG:-dev}" in
  dev) OUT=out/Koan; ARGS="$ROOT/build/koan.gn" ;;
  release) OUT=out/Release; ARGS="$ROOT/build/koan-release.gn" ;;
  *) echo "KOAN_CONFIG must be dev or release" >&2; exit 2 ;;
esac

cd "$ROOT/chromium/src"
mkdir -p "$OUT"
cp "$ARGS" "$OUT/args.gn"
gn gen "$OUT"
autoninja -C "$OUT" -j "$JOBS" "$TARGET"
