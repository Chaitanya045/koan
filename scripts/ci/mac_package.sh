#!/usr/bin/env bash
# macOS CI: packs the built Koan.app into a disk image.
#
#   scripts/ci/mac_package.sh <dest dir>
#
# Makes <dest dir>/Koan-<version>-<sha>-arm64.dmg and a .sha256 next to it.
# <version> is Koan's VERSION file when there is one, otherwise the Chromium
# version (CHROMIUM_VERSION, line 1), which is the version the app reports;
# <sha> is the Koan commit (KOAN_SHA, or HEAD), 7 characters.
#
# The app is signed ad hoc (codesign -s -): Apple Silicon only runs signed
# code, but nothing here has a Developer ID, so Gatekeeper still blocks it
# until the user allows it (docs/USAGE.md). Prints file_name=<dmg> to
# $GITHUB_OUTPUT when set. KOAN_CI_APP packs another bundle instead (the
# fetch job checks the packaging on a dummy app that way).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${1:?usage: $0 <dest dir>}"
# shellcheck source=scripts/ci/mac_env.sh
source "$ROOT/scripts/ci/mac_env.sh"
cd "$ROOT"

if [[ -f VERSION ]]; then
  version="$(tr -d '[:space:]' < VERSION)"
else
  version="$(sed -n 1p CHROMIUM_VERSION)"
fi
sha="${KOAN_SHA:-$(git rev-parse HEAD)}"
name="$(sed -n 's/^PRODUCT_FULLNAME=//p' chromium/src/chrome/app/theme/chromium/BRANDING)"
app="${KOAN_CI_APP:-chromium/src/out/Koan/$name.app}"
[[ -d $app ]] || { echo "no $app" >&2; exit 1; }
dmg="$name-$version-${sha:0:7}-arm64.dmg"

# Last, after everything that changes the bundle: --deep signs the nested
# frameworks, helper apps and libraries too, and --verify --deep --strict
# fails if any nested code is unsigned or its seal doesn't match.
echo "==> Signing $app (ad hoc)"
# The agent's `koan` and `koan-relay` sit in Resources/bin, where --deep does
# not look for code: each is signed on its own first, so the pairing dialog
# can name it.
for tool in koan koan-relay; do
  if [[ -f $app/Contents/Resources/bin/$tool ]]; then
    codesign --force --sign - --timestamp=none "$app/Contents/Resources/bin/$tool"
  fi
done
codesign --force --deep --sign - --timestamp=none "$app"
codesign --verify --deep --strict --verbose=2 "$app"
lipo -archs "$app/Contents/MacOS/$name"

echo "==> Making $dmg"
mkdir -p "$DEST"
stage="$(mktemp -d)"
ditto "$app" "$stage/$name.app"
ln -s /Applications "$stage/Applications"
rm -f "$DEST/$dmg"
# hdiutil now and then fails with "Resource busy" on CI runners; retry.
for attempt in 1 2 3; do
  if hdiutil create -volname "$name" -srcfolder "$stage" -fs HFS+ \
      -format ULFO -ov "$DEST/$dmg"; then
    break
  fi
  ((attempt < 3)) || exit 1
  sleep 20
done
rm -rf "$stage"
hdiutil verify "$DEST/$dmg"
# And the copy users get: mount the image and check the app in it.
mnt="$(mktemp -d)"
hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$DEST/$dmg" > /dev/null
codesign --verify --deep --strict --verbose=2 "$mnt/$name.app"
hdiutil detach "$mnt" > /dev/null
rmdir "$mnt"
(cd "$DEST" && shasum -a 256 "$dmg" > "$dmg.sha256" && cat "$dmg.sha256")
du -sh "$app" "$DEST/$dmg" | sed 's/^/size: /'

[[ -z ${GITHUB_OUTPUT:-} ]] || echo "file_name=$dmg" >> "$GITHUB_OUTPUT"
