#!/usr/bin/env bash
# Linux CI: packs and unpacks the build state that the chained build jobs
# hand on to each other as artifacts. Same format and reasons as
# scripts/ci/mac_archive.sh (POSIX tar, so siso sees nanosecond mtimes
# unchanged; the same GITHUB_WORKSPACE path on every runner).
#
#   scripts/ci/linux_archive.sh pack-source <file.tar.zst>
#   scripts/ci/linux_archive.sh pack-out <file.tar.zst>
#   scripts/ci/linux_archive.sh unpack <file.tar.zst>    (deletes the archive)
#
# source: the checkout except .git and chromium/src/out: chromium/ (patched
#         and branded), depot_tools/ and .cache (tool caches, build state).
# out:    chromium/src/out/Release.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODE="${1:-}"
FILE="${2:-}"
[[ -n $FILE ]] || { echo "usage: $0 pack-source|pack-out|unpack <file>" >&2; exit 2; }
cd "$ROOT"

pack() {
  mkdir -p "$(dirname "$FILE")"
  tar --format=posix -cf - "$@" | zstd -T0 -3 -q -o "$FILE"
  ls -l "$FILE"
}

case "$MODE" in
  pack-source)
    entries=()
    for e in * .[!.]*; do
      if [[ -e $e && $e != .git ]]; then entries+=("$e"); fi
    done
    # The CIPD cache only holds packages that are already installed.
    pack --anchored --exclude=./chromium/src/out \
      --exclude=./.cache/linux/cipd-cache "${entries[@]/#/./}"
    ;;
  pack-out)
    [[ -d chromium/src/out/Release ]] || { echo "no chromium/src/out/Release" >&2; exit 1; }
    pack ./chromium/src/out/Release
    ;;
  unpack)
    zstd -dc -q "$FILE" | tar -xf -
    rm -f "$FILE"
    ;;
  *)
    echo "usage: $0 pack-source|pack-out|unpack <file>" >&2
    exit 2
    ;;
esac
