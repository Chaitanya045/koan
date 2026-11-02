#!/usr/bin/env bash
# macOS CI: packs and unpacks the build state that the chained build jobs
# hand on to each other as artifacts.
#
#   scripts/ci/mac_archive.sh pack-source <file.tar.zst>
#   scripts/ci/mac_archive.sh pack-out <file.tar.zst>
#   scripts/ci/mac_archive.sh unpack <file.tar.zst>    (deletes the archive)
#
# source: the whole Koan checkout except .git and chromium/src/out, i.e.
#         chromium/ (patched and branded), depot_tools/ and the tool caches
#         in .cache/mac. Made once, by the fetch job.
# out:    chromium/src/out/Koan, made by every build job that runs out of
#         time.
#
# Every file keeps its mtime to the nanosecond (POSIX tar format): siso
# decides what to rebuild by mtime, so a tree unpacked on the next runner
# looks exactly like the one the previous runner left. The paths are the
# same on every runner (GITHUB_WORKSPACE), which siso and gn also rely on.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODE="${1:-}"
FILE="${2:-}"
[[ -n $FILE ]] || { echo "usage: $0 pack-source|pack-out|unpack <file>" >&2; exit 2; }
cd "$ROOT"

# GNU tar (gtar on the runners): BSD tar has no --format=posix with
# nanosecond times.
TAR="$(command -v gtar || command -v tar)"
"$TAR" --version | grep -q 'GNU tar' || { echo "needs GNU tar" >&2; exit 1; }

pack() {
  mkdir -p "$(dirname "$FILE")"
  "$TAR" --format=posix -cf - "$@" | zstd -T0 -3 -q -o "$FILE"
  ls -l "$FILE"
}

case "$MODE" in
  pack-source)
    entries=()
    for e in * .[!.]*; do
      if [[ -e $e && $e != .git ]]; then entries+=("$e"); fi
    done
    pack --anchored --exclude=./chromium/src/out --exclude=./.cache/mac/tmp \
      "${entries[@]/#/./}"
    ;;
  pack-out)
    [[ -d chromium/src/out/Koan ]] || { echo "no chromium/src/out/Koan" >&2; exit 1; }
    pack ./chromium/src/out/Koan
    ;;
  unpack)
    zstd -dc -q "$FILE" | "$TAR" -xf -
    rm -f "$FILE"
    ;;
  *)
    echo "usage: $0 pack-source|pack-out|unpack <file>" >&2
    exit 2
    ;;
esac
