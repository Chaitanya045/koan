#!/usr/bin/env bash
# Checks that Koan's GN files are `gn format`-clean. GN files are formatted with
# `gn format`, never with clang-format (which reads them as C++ and corrupts the
# comments).
#
#   scripts/ci/check_gn_format.sh            # downloads the gn Chromium pins
#   GN=/path/to/gn scripts/ci/check_gn_format.sh
#
# Files listed in scripts/ci/gn_format_exceptions.txt predate this check and are
# skipped; every other tracked .gn/.gni file, including every new one, must be
# clean. Fix a file with `gn format <file>` (and remove it from the exceptions
# when it is clean). The gn version is the one in Chromium's DEPS (gn_version);
# bump GN_VERSION when a roll changes it.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GN_VERSION=git_revision:938087829fcbcc8fd87835ee301dd522c7637685

if [[ -z ${GN:-} ]]; then
  WORK="$(mktemp -d)"
  trap 'rm -rf "$WORK"' EXIT
  curl -sSL --retry 6 --retry-delay 10 --retry-max-time 180 -o "$WORK/gn.zip" \
    "https://chrome-infra-packages.appspot.com/dl/gn/gn/linux-amd64/+/$GN_VERSION"
  unzip -q -o "$WORK/gn.zip" gn -d "$WORK"
  chmod +x "$WORK/gn"
  GN="$WORK/gn"
fi

cd "$ROOT"
status=0
while IFS= read -r file; do
  if grep -qxF "$file" scripts/ci/gn_format_exceptions.txt; then
    continue
  fi
  rc=0
  "$GN" format --dry-run "$file" >/dev/null 2>&1 || rc=$?
  if [[ $rc != 0 ]]; then
    echo "not gn-format clean: $file (run: gn format $file)"
    status=1
  fi
done < <(git ls-files '*.gn' '*.gni')
[[ $status == 0 ]] && echo "ok: GN files are gn-format clean"
exit "$status"
