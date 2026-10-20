#!/usr/bin/env bash
# Adds Koan to the desktop's application menu for the current user:
# icons in ~/.local/share/icons/hicolor and ~/.local/share/applications/
# koan.desktop pointing at the local build. The .desktop name matches Koan's
# WM_CLASS / Wayland app_id ("koan"), so windows group under the launcher.
#
#   install_desktop.sh               install / update
#   install_desktop.sh --uninstall   remove both
set -euo pipefail

if [[ $(uname -s) != Linux ]]; then
  echo "install_desktop.sh is for Linux desktops; on macOS, run Koan.app" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHROME="$ROOT/chromium/src/out/Koan/chrome"
ART="$ROOT/branding/out"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
DESKTOP="$DATA/applications/koan.desktop"
SIZES=(16 22 24 32 48 64 128 256 512)

refresh() {
  command -v update-desktop-database >/dev/null &&
    update-desktop-database "$DATA/applications" 2>/dev/null || true
  command -v gtk-update-icon-cache >/dev/null &&
    gtk-update-icon-cache -q -t "$DATA/icons/hicolor" 2>/dev/null || true
}

if [[ ${1:-} == --uninstall ]]; then
  rm -f "$DESKTOP"
  for s in "${SIZES[@]}"; do
    rm -f "$DATA/icons/hicolor/${s}x${s}/apps/koan.png"
  done
  refresh
  echo "removed Koan launcher and icons"
  exit 0
fi

[[ -x $CHROME ]] || { echo "no build at $CHROME (run scripts/build.sh)" >&2; exit 1; }
[[ -f $ART/logo_256.png ]] || "$ROOT/scripts/apply_branding.sh" >/dev/null

for s in "${SIZES[@]}"; do
  install -Dm644 "$ART/logo_$s.png" "$DATA/icons/hicolor/${s}x${s}/apps/koan.png"
done

mkdir -p "$(dirname "$DESKTOP")"
cat > "$DESKTOP" <<EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=Koan
GenericName=Web Browser
Comment=A calm, Zen-style browser built on Chromium
Exec="$CHROME" %U
Icon=koan
Terminal=false
StartupNotify=true
StartupWMClass=koan
Categories=Network;WebBrowser;
MimeType=text/html;text/xml;application/xhtml+xml;x-scheme-handler/http;x-scheme-handler/https;
Actions=new-window;new-private-window;

[Desktop Action new-window]
Name=New Window
Exec="$CHROME"

[Desktop Action new-private-window]
Name=New Incognito Window
Exec="$CHROME" --incognito
EOF
refresh
echo "installed $DESKTOP"
