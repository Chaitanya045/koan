#!/usr/bin/env bash
# Renders the Koan logos from branding/*.svg and rebrands chromium/src
# (product name strings, BRANDING file, logos). Idempotent.
#
#   apply_branding.sh            render + apply
#   apply_branding.sh --revert   restore the pristine Chromium files
#   apply_branding.sh --list     files owned by branding (excluded from
#                                apply_patches.sh export)
#
# Don't run while a build is running.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ART="$ROOT/branding/out"

case "${1:-}" in
  --revert) exec python3 -I "$ROOT/scripts/branding.py" revert ;;
  --list) exec python3 -I "$ROOT/scripts/branding.py" list ;;
esac

mkdir -p "$ART"
cd "$ROOT/branding"
for s in 16 22 24 32 48 64 128 256 512; do
  rsvg-convert -w "$s" -h "$s" logo.svg -o "$ART/logo_$s.png"
done
magick "$ART/logo_32.png" "$ART/logo_32.xpm"
rsvg-convert -w 22 -h 22 logo_mono.svg -o "$ART/logo_22_mono.png"
for variant in dark:#3c3c43 white:#ffffff; do
  name=${variant%%:*}; color=${variant#*:}
  suffix=$([[ $name == white ]] && echo _white || true)
  svg="$(mktemp --suffix=.svg -p "$ART")"
  sed "s/COLOR/$color/" wordmark.svg > "$svg"
  rsvg-convert -w 97 -h 22 "$svg" -o "$ART/wordmark_22$suffix.png"
  rsvg-convert -w 194 -h 44 "$svg" -o "$ART/wordmark_44$suffix.png"
  rm -f "$svg"
done

exec python3 -I "$ROOT/scripts/branding.py" apply
