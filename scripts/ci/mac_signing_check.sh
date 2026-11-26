#!/usr/bin/env bash
# macOS CI: helpers of the signing checks (mac-signing-check.yml).
#
#   scripts/ci/mac_signing_check.sh dummy-app <dest dir>
#   scripts/ci/mac_signing_check.sh check-image <.dmg> [<signer SHA-1>]
#
# dummy-app: makes <dest dir>/Koan.app, a copy of /usr/bin/true with an
#   Info.plist that carries the version VERSION wants, so mac_package.sh
#   (KOAN_PACKAGE_APP) passes its version check on it. Prints app=<path> (and
#   writes it to $GITHUB_OUTPUT when set).
# check-image: mounts the .dmg read-only and runs the checks users' Macs
#   would: codesign --verify --deep --strict on the app in it. With a
#   SHA-1 it also requires the app's designated requirement to be that
#   certificate's (certificate leaf = H"<sha1>"), so every release is the
#   same app to macOS.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

dummy_app() {
  local dest="${1:?usage: $0 dummy-app <dest dir>}" app plist chromium
  app="$dest/Koan.app"
  plist="$app/Contents/Info.plist"
  chromium="$(sed -n 1p "$ROOT/CHROMIUM_VERSION")"
  mkdir -p "$app/Contents/MacOS"
  cp /usr/bin/true "$app/Contents/MacOS/Koan"
  plutil -create xml1 "$plist"
  plutil -insert CFBundleExecutable -string Koan "$plist"
  plutil -insert CFBundleIdentifier -string dev.koan.Koan.ci-check "$plist"
  plutil -insert CFBundlePackageType -string APPL "$plist"
  plutil -insert KoanVersion -string \
    "$(python3 -I "$ROOT/src/version/koan_version.py" show full)" "$plist"
  plutil -insert CFBundleShortVersionString -string \
    "$(python3 -I "$ROOT/src/version/koan_version.py" show short)" "$plist"
  plutil -insert CFBundleVersion -string \
    "$(python3 -I "$ROOT/src/version/koan_version.py" show bundle --chromium "$chromium")" "$plist"
  echo "app=$app"
  [[ -z ${GITHUB_OUTPUT:-} ]] || echo "app=$app" >> "$GITHUB_OUTPUT"
}

check_image() {
  local dmg="${1:?usage: $0 check-image <.dmg> [<signer SHA-1>]}" want="${2:-}"
  local mnt app req rc=0
  mnt="$(mktemp -d)"
  hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$dmg" > /dev/null
  app="$(find "$mnt" -maxdepth 1 -name '*.app' | head -n 1)"
  codesign --verify --deep --strict --verbose=2 "$app" || rc=$?
  req="$(codesign -d -r- "$app" 2>&1)" || rc=$?
  printf '%s\n' "$req"
  if ((rc == 0)) && [[ -n $want ]]; then
    want="$(printf '%s' "$want" | tr '[:upper:]' '[:lower:]')"
    if ! printf '%s\n' "$req" | grep -q "certificate leaf = H\"$want\""; then
      echo "the app's designated requirement is not certificate leaf = H\"$want\"" >&2
      rc=1
    fi
  fi
  hdiutil detach "$mnt" > /dev/null
  rmdir "$mnt"
  return "$rc"
}

case "${1:-}" in
  dummy-app) shift; dummy_app "$@" ;;
  check-image) shift; check_image "$@" ;;
  *) echo "usage: $0 dummy-app <dest dir> | check-image <.dmg> [<sha1>]" >&2; exit 2 ;;
esac
