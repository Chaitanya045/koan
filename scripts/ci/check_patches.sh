#!/usr/bin/env bash
# Checks that every hook patch in patches/ applies cleanly to a Chromium
# revision, without a Chromium checkout: only the patched files are fetched
# from chromium.googlesource.com.
#
#   scripts/ci/check_patches.sh               # the pin (CHROMIUM_VERSION, line 2)
#   scripts/ci/check_patches.sh 158.0.8127.2  # a release tag
#   scripts/ci/check_patches.sh 681cd621e82e606d7536eae8ea2576afb3c7d65c
#   scripts/ci/check_patches.sh refs/branch-heads/8127   # any ref
#
# One line per patch: ok, CONFLICT (with git's reason), MISSING (the upstream
# file doesn't exist at that revision) or FETCH FAILED. Exits 0 if every
# patch is ok, 1 if not, 2 if the revision doesn't exist (no such tag, maybe
# not yet), 3 if googlesource couldn't be reached (try again).
#
# Files at a commit never change, so with KOAN_CHROMIUM_FILE_CACHE set they
# are kept there (CI caches it) and only new ones are fetched, with backoff on
# 429s. (The chromium/chromium GitHub mirror rate-limits every request, even
# authenticated ones, so it is not used.)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
# shellcheck source=scripts/koan_chromium.sh
source "$ROOT/scripts/koan_chromium.sh"
koan_require_env
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

TARGET="${1:-$(koan_pin_commit)}"
rc=0
COMMIT="$(koan_resolve_commit "$TARGET")" || rc=$?
if [[ $rc == 1 ]]; then
  echo "no Chromium revision '$TARGET'" >&2
  exit 2
elif [[ $rc != 0 ]] || ! koan_is_commit "$COMMIT"; then
  echo "cannot reach chromium.googlesource.com to resolve '$TARGET'" >&2
  exit 3
fi
if [[ -z ${1:-} ]]; then
  # CHROMIUM_VERSION's two lines must agree: tag -> commit.
  tagged="$(koan_resolve_commit "$(koan_pin_version)")" || tagged=
  if [[ -n $tagged && $tagged != "$COMMIT" ]]; then
    echo "CHROMIUM_VERSION: tag $(koan_pin_version) is $tagged, not $COMMIT" >&2
    exit 2
  fi
fi
echo "Checking patches against Chromium $TARGET ($COMMIT)"

CACHE="${KOAN_CHROMIUM_FILE_CACHE:+$KOAN_CHROMIUM_FILE_CACHE/$COMMIT}"
# fetch <file>: puts the revision's <file> at ./<file>. Returns 1 if the
# file doesn't exist there, 2 if it couldn't be fetched.
fetch() {
  if [[ -n $CACHE && -f $CACHE/$1 ]]; then
    cp "$CACHE/$1" "$1"
    return
  fi
  local http
  # ?format=TEXT serves the file base64-encoded.
  http="$(curl -sSL --retry 6 --retry-delay 10 --retry-max-time 180 \
    -o "$1.b64" -w '%{http_code}' \
    "$KOAN_GITILES/+/$COMMIT/$1?format=TEXT")" || return 2
  [[ $http == 404 ]] && return 1
  [[ $http == 200 ]] || return 2
  base64 -d <"$1.b64" >"$1" || return 2
  rm -f "$1.b64"
  if [[ -n $CACHE ]]; then
    # Via a temporary name: a killed run must not leave a truncated file.
    mkdir -p "$(dirname "$CACHE/$1")"
    cp "$1" "$CACHE/$1.tmp.$$" && mv "$CACHE/$1.tmp.$$" "$CACHE/$1" || true
  fi
  return 0
}

cd "$WORK"
git init -q
status=0
while IFS= read -r patch; do
  rel="${patch#"$ROOT/patches/"}"
  file="${rel%.patch}"
  mkdir -p "$(dirname "$file")"
  rc=0
  fetch "$file" || rc=$?
  if [[ $rc == 1 ]]; then
    echo "MISSING       $file"
    status=1
    continue
  elif [[ $rc != 0 ]]; then
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
done < <(find "$ROOT/patches" -name '*.patch' -type f | LC_ALL=C sort)
exit "$status"
