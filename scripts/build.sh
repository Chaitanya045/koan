#!/usr/bin/env bash
# Configure and build Koan.
#
# Linux:
#   scripts/build.sh [target]                       development build (out/Koan)
#   KOAN_CONFIG=release scripts/build.sh [target]   release build (out/Release)
#
#   Development: component build with DCHECKs, fast incremental rebuilds.
#   Release: one optimised binary, no DCHECKs, what gets packaged and shipped.
#
# macOS (Apple Silicon):
#   scripts/build.sh [target]                       build/koan-mac.gn (out/Koan)
#
#   One optimised build, Koan.app. Needs a full Xcode: DEVELOPER_DIR (default
#   /Applications/Xcode.app/Contents/Developer) is used, not xcode-select.
#
# JOBS defaults to 4 so a build fits next to other apps in 16 GB of RAM.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
export PATH="$ROOT/depot_tools:$PATH"
JOBS="${JOBS:-4}"
TARGET="${1:-chrome}"
OS="$(koan_os)"

if [[ $OS == mac ]]; then
  case "${KOAN_CONFIG:-mac}" in
    mac) OUT=out/Koan; ARGS="$ROOT/build/koan-mac.gn" ;;
    *) echo "on macOS, leave KOAN_CONFIG unset (build/koan-mac.gn)" >&2
       exit 2 ;;
  esac
  koan_require_xcode
  echo "Xcode: $DEVELOPER_DIR"
else
  case "${KOAN_CONFIG:-dev}" in
    dev) OUT=out/Koan; ARGS="$ROOT/build/koan.gn" ;;
    release) OUT=out/Release; ARGS="$ROOT/build/koan-release.gn" ;;
    *) echo "KOAN_CONFIG must be dev or release" >&2; exit 2 ;;
  esac
fi

cd "$ROOT/chromium/src"
mkdir -p "$OUT"
cp "$ARGS" "$OUT/args.gn"
gn gen "$OUT"
autoninja -C "$OUT" -j "$JOBS" "$TARGET"

if [[ $TARGET == chrome ]]; then
  if [[ $OS == mac ]]; then
    # The bundle is named after PRODUCT_FULLNAME in the BRANDING file
    # (Koan once scripts/apply_branding.sh has run, Chromium before).
    name="$(sed -n 's/^PRODUCT_FULLNAME=//p' chrome/app/theme/chromium/BRANDING)"
    echo "built $PWD/$OUT/$name.app"
    echo "run:  open \"$PWD/$OUT/$name.app\""
  else
    echo "built $PWD/$OUT/chrome"
  fi
fi
