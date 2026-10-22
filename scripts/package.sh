#!/usr/bin/env bash
# Packages a release build into a relocatable Linux tarball.
#
#   scripts/package.sh            -> dist/koan-<VERSION>-linux-x64.tar.xz
#                                    + dist/SHA256SUMS
#
# Takes out/Release (KOAN_CONFIG=release scripts/build.sh). The file list
# follows what Chromium's own Linux installer stages
# (chrome/installer/linux/common/installer.py); optional files are included
# when the build produced them. The tarball unpacks to koan-<VERSION>/ with
# the browser, a `koan` launcher, install.sh/uninstall.sh, icons and a
# .desktop file.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/chromium/src/${KOAN_OUT:-out/Release}"
VERSION="$(cat "$ROOT/VERSION")"
NAME="koan-$VERSION"
DIST="$ROOT/dist"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

[[ -x "$OUT/chrome" ]] || { echo "no release build at $OUT" >&2; exit 1; }
if grep -q 'is_component_build = true' "$OUT/args.gn" 2>/dev/null; then
  echo "$OUT is a component build; package a release build" >&2
  exit 1
fi

APP="$STAGE/$NAME"
mkdir -p "$APP"

required=(chrome chrome_crashpad_handler icudtl.dat resources.pak
          chrome_100_percent.pak chrome_200_percent.pak)
optional=(v8_context_snapshot.bin snapshot_blob.bin libEGL.so libGLESv2.so
          libvk_swiftshader.so vk_swiftshader_icd.json libvulkan.so.1
          libqt5_shim.so libqt6_shim.so libLiteRtWebGpuAccelerator.so
          liboptimization_guide_internal.so chrome_sandbox
          theme_resources_100_percent.pak ui_resources_100_percent.pak)
optional_dirs=(locales MEIPreload PrivacySandboxAttestationsPreloaded
               WidevineCdm)

for f in "${required[@]}"; do
  [[ -e "$OUT/$f" ]] || { echo "missing $OUT/$f" >&2; exit 1; }
  cp -a "$OUT/$f" "$APP/"
done
for f in "${optional[@]}"; do
  [[ -e "$OUT/$f" ]] && cp -a "$OUT/$f" "$APP/"
done
for d in "${optional_dirs[@]}"; do
  [[ -d "$OUT/$d" ]] && cp -a "$OUT/$d" "$APP/"
done
# Only shipped UI languages, not their debug info.
find "$APP/locales" -name '*.pak.info' -delete 2>/dev/null || true
strip --strip-unneeded "$APP/chrome" "$APP/chrome_crashpad_handler" 2>/dev/null || true

# Icons, launcher, desktop entry, install scripts, notices.
mkdir -p "$APP/icons"
for s in 16 22 24 32 48 64 128 256 512; do
  cp "$ROOT/branding/out/logo_$s.png" "$APP/icons/koan_$s.png"
done
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$APP/"
cp "$ROOT/docs/USAGE.md" "$APP/USAGE.md"
echo "$VERSION" > "$APP/VERSION"

cat > "$APP/koan" <<'EOF'
#!/usr/bin/env bash
# Starts Koan from wherever this directory lives.
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
exec "$HERE/chrome" "$@"
EOF
chmod +x "$APP/koan"

cat > "$APP/install.sh" <<'EOF'
#!/usr/bin/env bash
# Installs Koan for the current user: ~/.local/opt/koan, a `koan` command in
# ~/.local/bin, an app-menu entry and icons. Re-run to update.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
DEST="$HOME/.local/opt/koan"
rm -rf "$DEST.new"
mkdir -p "$(dirname "$DEST")"
cp -a "$HERE" "$DEST.new"
rm -rf "$DEST"
mv "$DEST.new" "$DEST"
mkdir -p "$HOME/.local/bin"
ln -sfn "$DEST/koan" "$HOME/.local/bin/koan"
for s in 16 22 24 32 48 64 128 256 512; do
  install -Dm644 "$DEST/icons/koan_$s.png" \
    "$DATA/icons/hicolor/${s}x${s}/apps/koan.png"
done
install -Dm644 "$DEST/koan.desktop" "$DATA/applications/koan.desktop"
sed -i "s|@KOAN@|$DEST/koan|g" "$DATA/applications/koan.desktop"
command -v update-desktop-database >/dev/null &&
  update-desktop-database "$DATA/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null &&
  gtk-update-icon-cache -q -t "$DATA/icons/hicolor" 2>/dev/null || true
echo "Koan $(cat "$DEST/VERSION") installed to $DEST; run 'koan' or use the app menu."
EOF
cat > "$APP/uninstall.sh" <<'EOF'
#!/usr/bin/env bash
# Removes what install.sh added. Your profile (~/.config/koan) is kept.
set -euo pipefail
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
rm -rf "$HOME/.local/opt/koan"
rm -f "$HOME/.local/bin/koan" "$DATA/applications/koan.desktop"
for s in 16 22 24 32 48 64 128 256 512; do
  rm -f "$DATA/icons/hicolor/${s}x${s}/apps/koan.png"
done
echo "Koan removed (profile kept in ~/.config/koan)."
EOF
chmod +x "$APP/install.sh" "$APP/uninstall.sh"

cat > "$APP/koan.desktop" <<'EOF'
[Desktop Entry]
Version=1.0
Type=Application
Name=Koan
GenericName=Web Browser
Comment=A calm, Zen-style browser built on Chromium
Exec=@KOAN@ %U
Icon=koan
Terminal=false
StartupNotify=true
StartupWMClass=koan
Categories=Network;WebBrowser;
MimeType=text/html;text/xml;application/xhtml+xml;x-scheme-handler/http;x-scheme-handler/https;
Actions=new-window;new-private-window;

[Desktop Action new-window]
Name=New Window
Exec=@KOAN@

[Desktop Action new-private-window]
Name=New Private Window
Exec=@KOAN@ --incognito
EOF

mkdir -p "$DIST"
tarball="$DIST/$NAME-linux-x64.tar.xz"
tar -C "$STAGE" --owner=0 --group=0 --sort=name -cf - "$NAME" |
  xz -T0 -9 > "$tarball"
(cd "$DIST" && sha256sum "$(basename "$tarball")" > SHA256SUMS)
echo "packaged $tarball ($(du -h "$tarball" | cut -f1))"
cat "$DIST/SHA256SUMS"
