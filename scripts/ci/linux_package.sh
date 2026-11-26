#!/usr/bin/env bash
# Linux: packs a Koan release build (out/Release) into a Flatpak bundle.
#
#   scripts/ci/linux_package.sh <dest dir>
#
# Makes <dest dir>/Koan-<version>-<sha>-x86_64.flatpak and a .sha256 next to
# it, or Koan-<version>-x86_64.flatpak with KOAN_RELEASE=1. <version> is
# Koan's VERSION file; <sha> is the Koan commit (KOAN_SHA, or HEAD), 7
# characters. Prints file_name=<bundle> to $GITHUB_OUTPUT when set.
#
# Stages the files Chromium's own Linux installer ships
# (chrome/installer/linux/common/installer.include) into koan/, next to a
# copy of flatpak/ (the manifest, launcher, desktop entry, metainfo) and the
# icons rendered from branding/logo.svg, then runs flatpak-builder (the
# runtime, SDK and org.chromium.Chromium.BaseApp come from Flathub, per
# user) and flatpak build-bundle. Needs flatpak, rsvg-convert and
# Flathub's org.flatpak.Builder (flatpak install --user flathub
# org.flatpak.Builder), else a flatpak-builder on PATH. Works outside CI too.
#
# KOAN_CI_OUT=<dir> packs another build dir (any Chromium Linux build with
# the same layout). KOAN_CI_DUMMY=1 packs a fake build whose browser is a
# shell script: the fetch job checks the manifest and the Flathub downloads
# that way, hours before the real build needs them.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${1:?usage: $0 <dest dir>}"
cd "$ROOT"
APP_ID=dev.koan.Koan
for tool in flatpak rsvg-convert; do
  command -v "$tool" >/dev/null || { echo "needs $tool" >&2; exit 1; }
done

version="$(tr -d '[:space:]' < VERSION)"
sha="${KOAN_SHA:-$(git rev-parse HEAD)}"
if [[ ${KOAN_RELEASE:-0} == 1 ]]; then
  bundle="Koan-$version-x86_64.flatpak"
else
  bundle="Koan-$version-${sha:0:7}-x86_64.flatpak"
fi

WORK="${KOAN_SCRATCH:-$ROOT/.cache}/flatpak"
rm -rf "$WORK"
mkdir -p "$WORK/build/koan" "$DEST"
STAGE="$WORK/build/koan"

if [[ ${KOAN_CI_DUMMY:-0} == 1 ]]; then
  OUT="$WORK/dummy-out"
  mkdir -p "$OUT/locales"
  printf '#!/bin/sh\necho "Koan packaging check: $*"\n' > "$OUT/chrome"
  cp "$OUT/chrome" "$OUT/chrome_crashpad_handler"
  chmod +x "$OUT/chrome" "$OUT/chrome_crashpad_handler"
  for f in icudtl.dat resources.pak chrome_100_percent.pak chrome_200_percent.pak \
      locales/en-US.pak; do
    : > "$OUT/$f"
  done
  echo "is_component_build = false" > "$OUT/args.gn"
else
  OUT="${KOAN_CI_OUT:-$ROOT/chromium/src/out/Release}"
fi

[[ -x $OUT/chrome ]] || { echo "no Koan build at $OUT" >&2; exit 1; }
if grep -q 'is_component_build = true' "$OUT/args.gn" 2>/dev/null; then
  echo "$OUT is a component build; package a release build" >&2
  exit 1
fi

echo "==> Staging $OUT"
required=(chrome chrome_crashpad_handler icudtl.dat resources.pak
          chrome_100_percent.pak chrome_200_percent.pak locales)
optional=(v8_context_snapshot.bin snapshot_blob.bin libEGL.so libGLESv2.so
          libvk_swiftshader.so vk_swiftshader_icd.json libvulkan.so.1
          libqt5_shim.so libqt6_shim.so libqt6_shim.so.6 libffmpeg.so
          MEIPreload PrivacySandboxAttestationsPreloaded WidevineCdm
          resources koan)
for f in "${required[@]}"; do
  [[ -e $OUT/$f ]] || { echo "missing $OUT/$f" >&2; exit 1; }
  cp -a "$OUT/$f" "$STAGE/"
done
for f in "${optional[@]}"; do
  if [[ -e $OUT/$f ]]; then cp -a "$OUT/$f" "$STAGE/"; fi
done
find "$STAGE/locales" -name '*.pak.info' -delete
if [[ ${KOAN_CI_DUMMY:-0} != 1 ]]; then
  strip --strip-unneeded "$STAGE/chrome" "$STAGE/chrome_crashpad_handler"
fi
cp LICENSE THIRD_PARTY_NOTICES.md "$STAGE/"
du -sh "$STAGE"

echo "==> Flatpak files and icons"
cp flatpak/* "$WORK/build/"
sed -i "s/@VERSION@/$version/; s/@DATE@/$(date -u +%F)/" \
  "$WORK/build/$APP_ID.metainfo.xml"
cp branding/logo.svg "$WORK/build/$APP_ID.svg"
for s in 64 128 256 512; do
  rsvg-convert -w "$s" -h "$s" branding/logo.svg -o "$WORK/build/icon-$s.png"
done

echo "==> flatpak-builder"
flatpak remote-add --user --if-not-exists flathub \
  https://dl.flathub.org/repo/flathub.flatpakrepo
if flatpak info --user org.flatpak.Builder >/dev/null 2>&1; then
  builder=(flatpak run "--filesystem=$ROOT" "--filesystem=$WORK" org.flatpak.Builder)
  # The sandboxed builder runs flatpak on the host through the session
  # bus, which a CI runner (no desktop session) doesn't have.
  if [[ -z ${DBUS_SESSION_BUS_ADDRESS:-} ]]; then
    builder=(dbus-run-session -- "${builder[@]}")
  fi
elif command -v flatpak-builder >/dev/null; then
  builder=(flatpak-builder)
else
  echo "needs org.flatpak.Builder (flatpak install --user flathub org.flatpak.Builder)" >&2
  exit 1
fi
# rofiles-fuse needs FUSE, which containers and some runners lack.
"${builder[@]}" --user --install-deps-from=flathub --disable-rofiles-fuse \
  --force-clean --repo="$WORK/repo" --state-dir="$WORK/state" \
  "$WORK/app" "$WORK/build/$APP_ID.yml"

echo "==> Bundle"
flatpak build-bundle --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo \
  "$WORK/repo" "$DEST/$bundle" "$APP_ID"
(cd "$DEST" && sha256sum "$bundle" > "$bundle.sha256")
ls -l "$DEST/$bundle"
cat "$DEST/$bundle.sha256"
rm -rf "$WORK"

echo "file_name=$bundle"
[[ -z ${GITHUB_OUTPUT:-} ]] || echo "file_name=$bundle" >> "$GITHUB_OUTPUT"
