#!/usr/bin/env bash
# Renders the Koan logos from branding/*.svg and rebrands chromium/src
# (product name strings, BRANDING file, logos). Idempotent.
#
#   apply_branding.sh               render + apply
#   apply_branding.sh --revert      restore the pristine Chromium files
#   apply_branding.sh --list        files owned by branding (excluded from
#                                   apply_patches.sh export)
#   apply_branding.sh --render-mac  Linux: render the PNGs macOS needs into
#                                   branding/mac/rendered/ (commit them after
#                                   changing branding/*.svg)
#
# Linux: renders with rsvg-convert (librsvg) and magick (ImageMagick, for the
# XPM icon).
# macOS: renders nothing. It uses the committed PNGs in
# branding/mac/rendered/, and builds the app icon with iconutil and actool
# (full Xcode, DEVELOPER_DIR). Run it with the folder's env.sh sourced
# (.agent/notes/mac.md). Works with bash 3.2 and BSD tools.
#
# Don't run while a build is running.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ART="$ROOT/branding/out"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"

branding() { python3 -I "$ROOT/scripts/branding.py" "$@"; }

case "${1:-}" in
  --revert) koan_require_env; branding revert; exit ;;
  --list) branding list; exit ;;
  --render-mac | "") ;;
  *) echo "usage: $0 [--revert|--list|--render-mac]" >&2; exit 2 ;;
esac

koan_require_env
OS="$(koan_os)"

if [[ $OS == mac ]]; then
  [[ -z ${1:-} ]] || { echo "--render-mac runs on Linux" >&2; exit 2; }
  branding load-mac-renders  # branding/mac/rendered -> branding/out
  branding mac-assets        # app.icns (iconutil) and Assets.car (actool)
  branding apply --platform mac
  exit
fi

command -v rsvg-convert >/dev/null ||
  { echo "rsvg-convert not found (install librsvg)" >&2; exit 1; }
render() { rsvg-convert -w "$2" -h "$3" "$1" -o "$4"; }

mkdir -p "$ART/mac"
TMP="$(mktemp -d "$ART/tmp.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

cd "$ROOT/branding"
for s in 16 22 24 32 48 64 128 256 512; do
  render logo.svg "$s" "$s" "$ART/logo_$s.png"
done
render logo_mono.svg 22 22 "$ART/logo_22_mono.png"
for variant in dark:#3c3c43 white:#ffffff; do
  name=${variant%%:*}; color=${variant#*:}
  suffix=$([[ $name == white ]] && echo _white || true)
  sed "s/COLOR/$color/" wordmark.svg > "$TMP/wordmark.svg"
  render "$TMP/wordmark.svg" 97 22 "$ART/wordmark_22$suffix.png"
  render "$TMP/wordmark.svg" 194 44 "$ART/wordmark_44$suffix.png"
done

# The macOS app icon bitmaps, on Apple's icon grid.
branding mac-icon-svg logo.svg > "$TMP/logo_mac.svg"
for s in $(branding mac-icon-sizes); do
  render "$TMP/logo_mac.svg" "$s" "$s" "$ART/mac/appicon_$s.png"
done

if [[ ${1:-} == --render-mac ]]; then
  branding save-mac-renders
  exit
fi
branding check-mac-renders  # warns if branding/mac/rendered is stale

magick "$ART/logo_32.png" "$ART/logo_32.xpm"
branding apply --platform linux
