#!/usr/bin/env bash
# Renders the Koan logos from branding/*.svg and rebrands chromium/src
# (product name strings, BRANDING file, logos). Idempotent.
#
#   apply_branding.sh            render + apply
#   apply_branding.sh --revert   restore the pristine Chromium files
#   apply_branding.sh --list     files owned by branding (excluded from
#                                apply_patches.sh export)
#
# Linux: needs rsvg-convert (librsvg) and magick (ImageMagick, for the XPM
# icon). macOS: needs a full Xcode (DEVELOPER_DIR; iconutil and actool build
# the app icon) and preferably rsvg-convert (brew install librsvg); without
# it the logos are rendered with sips. Works with bash 3.2 and BSD tools.
#
# Don't run while a build is running.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ART="$ROOT/branding/out"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"

branding() { python3 -I "$ROOT/scripts/branding.py" "$@"; }

case "${1:-}" in
  --revert) branding revert; exit ;;
  --list) branding list; exit ;;
esac

OS="$(koan_os)"
[[ $OS == mac ]] && koan_require_xcode

mkdir -p "$ART/mac"
TMP="$(mktemp -d "$ART/tmp.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

# render <svg> <width> <height> <out.png>
if command -v rsvg-convert >/dev/null; then
  render() { rsvg-convert -w "$2" -h "$3" "$1" -o "$4"; }
elif [[ $OS == mac ]]; then
  echo "rsvg-convert not found (brew install librsvg); rendering with sips" >&2
  render() {
    # sips rasterises an SVG at its width and height attributes.
    sed "s/<svg /<svg width=\"$2\" height=\"$3\" /" "$1" > "$TMP/sized.svg"
    sips -s format png "$TMP/sized.svg" --out "$4" >/dev/null
  }
else
  echo "rsvg-convert not found (install librsvg)" >&2
  exit 1
fi

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

# The macOS app icon bitmaps, on Apple's icon grid. Rendered everywhere so
# they can be checked on Linux too; only macOS turns them into an icon.
branding mac-icon-svg logo.svg > "$TMP/logo_mac.svg"
for s in $(branding mac-icon-sizes); do
  render "$TMP/logo_mac.svg" "$s" "$s" "$ART/mac/appicon_$s.png"
done

if [[ $OS == mac ]]; then
  branding mac-assets  # app.icns (iconutil) and Assets.car (actool)
else
  magick "$ART/logo_32.png" "$ART/logo_32.xpm"
fi

branding apply --platform "$OS"
