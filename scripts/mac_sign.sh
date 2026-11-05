#!/usr/bin/env bash
# Signs a Koan.app with Koan's local identity (scripts/mac_signing_identity.sh),
# so rebuilds keep the same designated requirement and the saved
# "Koan Safe Storage" Keychain item (the key for cookies and passwords)
# keeps matching. Without the identity it leaves the app as built (ad hoc,
# signed by the linker) and says so: saved logins may then need re-entering
# after a rebuild.
#
#   scripts/mac_sign.sh path/to/Koan.app
#
# scripts/build.sh runs this after a Mac build; KOAN_SIGN=0 skips it.
# Bash 3.2 (macOS's) compatible.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
if [[ $(koan_os) != mac ]]; then
  echo "macOS only" >&2
  exit 1
fi
koan_require_mac_env

app="${1:?usage: scripts/mac_sign.sh path/to/Koan.app}"
[[ -d $app ]] || { echo "no such app: $app" >&2; exit 1; }

keychain="$KOAN_MAC/keychains/koan-signing.keychain-db"
if ! hash="$("$ROOT/scripts/mac_signing_identity.sh" status)"; then
  echo "no signing identity: $app stays ad-hoc signed, so its signature" >&2
  echo "changes with every build and saved logins may reset after a rebuild." >&2
  echo "Run scripts/mac_signing_identity.sh create once to fix that." >&2
  exit 0
fi

security unlock-keychain -p "koan-local-signing" "$keychain"
# --deep re-signs the helper apps and frameworks inside too. No hardened
# runtime and no entitlements, like the linker-signed build it replaces.
codesign --force --deep --sign "$hash" --keychain "$keychain" \
  --timestamp=none "$app"
codesign --verify --deep --strict "$app"
echo "signed $app with Koan Local Signing ($hash)" >&2
codesign -d -r- "$app" 2>&1 | sed -n 's/^designated => /designated requirement: /p' >&2
