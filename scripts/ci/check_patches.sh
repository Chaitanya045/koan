#!/usr/bin/env bash
# Checks that every hook patch in patches/ applies cleanly to the pinned
# Chromium revision (CHROMIUM_VERSION, line 2), without a Chromium checkout:
# only the patched files are fetched from chromium.googlesource.com.
#
#   scripts/ci/check_patches.sh
#
# Files at a pinned commit never change, so with KOAN_CHROMIUM_FILE_CACHE set
# they are kept there (CI caches it) and only new ones are fetched, with
# backoff on 429s. (The chromium/chromium GitHub mirror rate-limits every
# request, even authenticated ones, so it is not used.)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
koan_require_env
COMMIT="$(sed -n 2p "$ROOT/CHROMIUM_VERSION")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

CACHE="${KOAN_CHROMIUM_FILE_CACHE:+$KOAN_CHROMIUM_FILE_CACHE/$COMMIT}"
# fetch <file>: puts the pinned revision of <file> at ./<file>.
fetch() {
  if [[ -n $CACHE && -f $CACHE/$1 ]]; then
    cp "$CACHE/$1" "$1"
    return
  fi
  # ?format=TEXT serves the file base64-encoded.
  curl -fsSL --retry 6 --retry-delay 10 --retry-max-time 180 \
    "https://chromium.googlesource.com/chromium/src/+/$COMMIT/$1?format=TEXT" |
    base64 -d >"$1" || return 1
  if [[ -n $CACHE ]]; then
    mkdir -p "$(dirname "$CACHE/$1")"
    cp "$1" "$CACHE/$1"
  fi
}

cd "$WORK"
git init -q
status=0
while IFS= read -r patch; do
  rel="${patch#"$ROOT/patches/"}"
  file="${rel%.patch}"
  mkdir -p "$(dirname "$file")"
  if ! fetch "$file"; then
    echo "FETCH FAILED  $file"
    status=1
    continue
  fi
  if git apply --check "$patch" 2>/dev/null; then
    echo "ok            $file"
  else
    echo "CONFLICT      $file"
    git apply --check "$patch" || true
    status=1
  fi
done < <(find "$ROOT/patches" -name '*.patch' -type f | sort)
exit "$status"
