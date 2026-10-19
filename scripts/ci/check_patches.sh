#!/usr/bin/env bash
# Checks that every hook patch in patches/ applies cleanly to the pinned
# Chromium revision (CHROMIUM_VERSION, line 2), without a Chromium checkout:
# only the patched files are fetched from the chromium/chromium GitHub mirror.
#
#   scripts/ci/check_patches.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
COMMIT="$(sed -n 2p "$ROOT/CHROMIUM_VERSION")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cd "$WORK"
git init -q
status=0
while IFS= read -r patch; do
  rel="${patch#"$ROOT/patches/"}"
  file="${rel%.patch}"
  mkdir -p "$(dirname "$file")"
  url="https://raw.githubusercontent.com/chromium/chromium/$COMMIT/$file"
  if ! curl -fsSL --retry 3 -o "$file" "$url"; then
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
