#!/usr/bin/env bash
# run_mac.sh [chrome flags...] [urls...]
#
# macOS: runs the local build (chromium/src/out/Koan/<name>.app) for testing,
# keeping what it writes inside the one folder (KOAN_MAC, env.sh sourced):
#
# - profile and cache: $KOAN_MAC/profile (--user-data-dir; outside
#   ~/Library/Application Support, Chromium keeps the cache in the profile);
# - no login-keychain item: --use-mock-keychain (KOAN_REAL_KEYCHAIN=1 uses
#   the real "Koan Safe Storage" item instead);
# - no code-signing clone under /var/folders (MacAppCodeSignClone off);
# - started directly rather than with `open`, so it inherits HOME and
#   CFFIXED_USER_HOME from the folder (preferences and saved window state go
#   there where macOS honours them).
#
# Your daily Koan (opened from Finder or the Dock) uses
# ~/Library/Application Support/Koan instead. Like any build or run on the
# maintainer's Mac, ask before running this.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
if [[ $(koan_os) != mac ]]; then
  echo "run_mac.sh is for macOS; on Linux run chromium/src/out/Koan/chrome" >&2
  exit 1
fi
koan_require_env

SRC="$ROOT/chromium/src"
name="$(sed -n 's/^PRODUCT_FULLNAME=//p' "$SRC/chrome/app/theme/chromium/BRANDING")"
app="$SRC/out/Koan/$name.app"
if [[ ! -x "$app/Contents/MacOS/$name" ]]; then
  echo "no build at $app (run scripts/build.sh)" >&2
  exit 1
fi

profile="$KOAN_MAC/profile"
mkdir -p "$profile"
flags=(--user-data-dir="$profile" --disable-features=MacAppCodeSignClone
  --no-default-browser-check)
[[ ${KOAN_REAL_KEYCHAIN:-} == 1 ]] || flags+=(--use-mock-keychain)

exec "$app/Contents/MacOS/$name" "${flags[@]}" "$@"
