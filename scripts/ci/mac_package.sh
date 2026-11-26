#!/usr/bin/env bash
# macOS CI: packs the built Koan.app into a disk image.
#
#   scripts/ci/mac_package.sh <dest dir>
#
# Makes <dest dir>/Koan-<version>-<sha>-arm64.dmg and a .sha256 next to it,
# or Koan-<version>-arm64.dmg with KOAN_RELEASE=1 (release.yml).
# <version> is Koan's VERSION file when there is one, otherwise the Chromium
# version (CHROMIUM_VERSION, line 1); <sha> is the Koan commit (KOAN_SHA, or
# HEAD), 7 characters. With VERSION, the app's Info.plist must carry it
# (KoanVersion, CFBundleShortVersionString: //koan/version), or nothing is
# packed.
#
# The app is signed ad hoc (codesign -s -) unless KOAN_CODESIGN_IDENTITY
# names an identity (a SHA-1 or a name; KOAN_CODESIGN_KEYCHAIN, the keychain
# it is in): Apple Silicon only runs signed code, but nothing here has a
# Developer ID, so Gatekeeper still blocks it until the user allows it
# (docs/USAGE.md). Releases are signed with the self-signed "Koan Release
# Signing" certificate (scripts/ci/mac_codesign.sh, release.yml); the
# signing authority and designated requirement are printed afterwards.
# Prints file_name=<dmg> to $GITHUB_OUTPUT when set.
#
# Two ways to pack a bundle that isn't the build's:
#   KOAN_CI_APP=<app>       a dummy app (the fetch job checks the packaging
#                           that way): no Info.plist check, no VMP step.
#   KOAN_PACKAGE_APP=<app>  a real Koan.app, e.g. the one in the build's ad
#                           hoc .dmg, re-signed by release.yml's publish job,
#                           which has no chromium/ checkout: the product name
#                           is the bundle's name, the Info.plist check and the
#                           Widevine VMP step still run.
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
if [[ -n ${KOAN_CI_APP:-} && -n ${KOAN_PACKAGE_APP:-} ]]; then
  echo "KOAN_CI_APP and KOAN_PACKAGE_APP are exclusive" >&2
  exit 1
fi
if [[ -n ${KOAN_PACKAGE_APP:-} ]]; then
  app="$KOAN_PACKAGE_APP"
  name="$(basename "$app" .app)"
else
  name="$(sed -n 's/^PRODUCT_FULLNAME=//p' chromium/src/chrome/app/theme/chromium/BRANDING)"
  app="${KOAN_CI_APP:-chromium/src/out/Koan/$name.app}"
fi
[[ -d $app ]] || { echo "no $app" >&2; exit 1; }
if [[ ${KOAN_RELEASE:-0} == 1 ]]; then
  [[ -f VERSION ]] || { echo "KOAN_RELEASE=1 needs a VERSION file" >&2; exit 1; }
  dmg="$name-$version-arm64.dmg"
else
  dmg="$name-$version-${sha:0:7}-arm64.dmg"
fi

# The version the app reports is the one being released (the build's
# Info.plist step, koan/version/version.gni, ran).
if [[ -z ${KOAN_CI_APP:-} && -f VERSION ]]; then
  plist="$app/Contents/Info.plist"
  want_short="$(python3 -I src/version/koan_version.py show short)"
  got="$(plutil -extract KoanVersion raw -o - "$plist" 2>/dev/null || true)"
  got_short="$(plutil -extract CFBundleShortVersionString raw -o - "$plist")"
  if [[ $got != "$version" || $got_short != "$want_short" ]]; then
    echo "$plist says KoanVersion '${got}', CFBundleShortVersionString" \
      "'$got_short'; VERSION wants '$version', '$want_short'" >&2
    exit 1
  fi
  echo "Info.plist: Koan $got ($got_short, CFBundleVersion" \
    "$(plutil -extract CFBundleVersion raw -o - "$plist"))"
fi

# Widevine's VMP signature goes into the framework's Resources, which the
# code signature below seals, so it comes first. Without the EVS secrets
# (EVS_ACCOUNT_NAME, EVS_PASSWD) the app stays without VMP.
if [[ -z ${KOAN_CI_APP:-} ]]; then
  scripts/mac/widevine_vmp_sign.sh --if-configured "$app"
fi

# Last, after everything that changes the bundle: --deep signs the nested
# frameworks, helper apps and libraries too, and --verify --deep --strict
# fails if any nested code is unsigned or its seal doesn't match.
identity="${KOAN_CODESIGN_IDENTITY:--}"
# release.yml re-signs the build's app for users: an empty identity there
# must fail, not fall back to ad hoc.
if [[ -n ${KOAN_PACKAGE_APP:-} && ${KOAN_RELEASE:-0} == 1 && $identity == - ]]; then
  echo "KOAN_PACKAGE_APP with KOAN_RELEASE=1 needs KOAN_CODESIGN_IDENTITY" >&2
  exit 1
fi
sign=(codesign --force --sign "$identity" --timestamp=none)
if [[ -n ${KOAN_CODESIGN_KEYCHAIN:-} ]]; then
  sign+=(--keychain "$KOAN_CODESIGN_KEYCHAIN")
fi
if [[ $identity == - ]]; then
  echo "==> Signing $app (ad hoc)"
else
  echo "==> Signing $app with $identity"
fi
# The agent's `koan` and `koan-relay` sit in Resources/bin, where --deep does
# not look for code: each is signed on its own first, so the pairing dialog
# can name it.
for tool in koan koan-relay; do
  if [[ -f $app/Contents/Resources/bin/$tool ]]; then
    "${sign[@]}" "$app/Contents/Resources/bin/$tool"
  fi
done
"${sign[@]}" --deep "$app"
codesign --verify --deep --strict --verbose=2 "$app"
# Who signed it, and the requirement a later update must meet (for a
# certificate: identifier plus the certificate's SHA-1).
codesign -d --verbose=2 "$app" 2>&1 | grep -E '^(Identifier|Authority|TeamIdentifier|Signature|CDHash)' || true
codesign -d -r- "$app" 2>&1 | grep -F 'designated =>' || true
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
