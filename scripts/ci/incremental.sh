#!/usr/bin/env bash
# CI: turns an earlier run's source tree into this commit's, touching only
# what changed, so a build on top of that run's finished out/ directory
# rebuilds only that.
#
#   GH_TOKEN=... GH_REPO=owner/repo KOAN_TOOLCHAIN=<id> \
#     KOAN_FULL_BUILD_INPUTS="<space-separated repo paths>" \
#     scripts/ci/incremental.sh <base run id> <artifact prefix> <scratch dir>
#   e.g. scripts/ci/incremental.sh 123 koan-mac "$RUNNER_TEMP"
#
# KOAN_FULL_BUILD_INPUTS lists the files and directories (relative to the
# repository root) whose change since the base means a full build: what the
# fetch bakes into the tree, that the tree has no .git to undo. The caller
# (mac_fetch.sh) owns the list. CHROMIUM_VERSION is always checked too.
#
# KOAN_BASE_ARCHIVE=<file> takes the base's source archive from a file
# instead of the run's artifact (tests).
#
# siso decides what to rebuild by mtime, and a fresh checkout gives every
# file a new one, so reusing out/ alone would rebuild everything. Instead,
# with this commit checked out (.git included), this:
#
# 1. deletes the checkout's tracked files and unpacks the base run's source
#    archive (the patched, branded Chromium tree, depot_tools and caches,
#    with their mtimes) in their place;
# 2. gives up (exit 3) if CHROMIUM_VERSION or any KOAN_FULL_BUILD_INPUTS path
#    differs from this commit's, or the toolchain (KOAN_TOOLCHAIN, e.g. the
#    Xcode and SDK; stored in .cache/ci-toolchain by the fetch) does: the
#    tree has no .git to undo branding or the fetch's steps with, and a new
#    Chromium or SDK is a full build anyway;
# 3. lets git restore what differs from this commit (git status against
#    the checkout's index), so only those files get a new mtime, and
#    deletes the base's files this commit doesn't have;
# 4. for each patch that changed, reverses the base's version and applies
#    this commit's, with git apply as scripts/apply_patches.sh does.
#
# Exits 0 when the tree is this commit's; anything else leaves a mixed tree
# that the caller wipes before a full fetch. Needs GNU tar (gtar on macOS)
# and zstd.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BASE="${1:?usage: $0 <base run id> <artifact prefix> <scratch dir>}"
PREFIX="${2:?usage: $0 <base run id> <artifact prefix> <scratch dir>}"
SCRATCH="${3:?usage: $0 <base run id> <artifact prefix> <scratch dir>}"
: "${KOAN_TOOLCHAIN:?set KOAN_TOOLCHAIN}" "${KOAN_FULL_BUILD_INPUTS:?set KOAN_FULL_BUILD_INPUTS}"
cd "$ROOT"
mkdir -p "$SCRATCH"

step() { echo "==> $* ($(date -u +%H:%M:%S))"; }
TAR="$(command -v gtar || command -v tar)"
"$TAR" --version | grep -q 'GNU tar' || { echo "needs GNU tar" >&2; exit 1; }

archive="$SCRATCH/base-source.tar.zst"
if [[ -n ${KOAN_BASE_ARCHIVE:-} ]]; then
  cp "$KOAN_BASE_ARCHIVE" "$archive"
else
  : "${GH_REPO:?set GH_REPO}"
  step "Downloading the source of run $BASE"
  id="$(gh api "repos/$GH_REPO/actions/runs/$BASE/artifacts?per_page=100" \
    --jq ".artifacts[] | select(.name == \"$PREFIX-source.tar.zst\" and (.expired | not)) | .id")"
  [[ -n $id ]] || { echo "run $BASE has no source archive any more" >&2; exit 3; }
  # An artifact uploaded with archive: false comes back as the file itself.
  gh api "repos/$GH_REPO/actions/artifacts/$id/zip" > "$archive"
fi
ls -l "$archive"

step "Unpacking it"
# The tracked files must be exactly the base's: files only this commit has
# would otherwise survive the unpack and look unchanged. .git stays.
git ls-files -z | xargs -0 rm -f
zstd -dc -q "$archive" | "$TAR" -xf -
rm -f "$archive"

if ! git diff --quiet -- CHROMIUM_VERSION; then
  echo "CHROMIUM_VERSION moved since run $BASE: a full build" >&2
  exit 3
fi
# git status, not git diff: it also sees files only the base has. Word-split
# on purpose (the paths have no spaces).
# shellcheck disable=SC2086
changed_inputs="$(git status --porcelain=v1 --untracked-files=all --no-renames -- $KOAN_FULL_BUILD_INPUTS)"
if [[ -n $changed_inputs ]]; then
  echo "inputs of the fetch changed since run $BASE: a full build" >&2
  echo "$changed_inputs" >&2
  exit 3
fi
if [[ "$(cat .cache/ci-toolchain 2>/dev/null)" != "$KOAN_TOOLCHAIN" ]]; then
  echo "run $BASE built with '$(cat .cache/ci-toolchain 2>/dev/null)', not '$KOAN_TOOLCHAIN': a full build" >&2
  exit 3
fi
old_patches="$SCRATCH/base-patches"
rm -rf "$old_patches"
cp -R patches "$old_patches"

step "Bringing in this commit's changes"
# Modified (M), missing (D) and type-changed (T) files come back from the
# index with a new mtime; files only the base has (??, not ignored) go.
# Everything else, including the ignored chromium/, depot_tools/ and
# .cache/, keeps the base's mtimes.
restored=0 removed=0
while IFS= read -r -d '' entry; do
  code="${entry:0:2}" path="${entry:3}"
  case "$code" in
    '??') rm -rf -- "$path"; echo "removed $path"; removed=$((removed + 1)) ;;
    *) git checkout -q -- "$path"; echo "updated $path"; restored=$((restored + 1)) ;;
  esac
done < <(git status --porcelain=v1 -z --untracked-files=all --no-renames)
echo "$restored files updated, $removed removed"
git status --porcelain=v1 | grep . && { echo "the tree still differs from HEAD" >&2; exit 1; }

step "Bringing Koan's patches up to date"
# git apply as in scripts/apply_patches.sh, but chromium/src has no .git
# any more: the ceiling keeps git from taking Koan's repository for it.
apply() { GIT_CEILING_DIRECTORIES="$ROOT" git -C chromium/src apply "$@"; }
changed=0
while IFS= read -r rel; do
  [[ -n $rel ]] || continue
  o="$old_patches/$rel" n="$ROOT/patches/$rel"
  if [[ -f $o && -f $n ]] && cmp -s "$o" "$n"; then continue; fi
  if [[ -f $o ]]; then
    apply --reverse --check "$o" || { echo "can't reverse the base's $rel" >&2; exit 3; }
    apply --reverse "$o"
  fi
  if [[ -f $n ]]; then
    apply --check "$n" || { echo "can't apply $rel" >&2; exit 3; }
    apply "$n"
  fi
  echo "updated ${rel%.patch}"
  changed=$((changed + 1))
done < <({ (cd "$old_patches" && find . -name '*.patch'); (cd patches && find . -name '*.patch'); } |
  sed 's#^\./##' | LC_ALL=C sort -u)
echo "$changed patches changed"
rm -rf "$old_patches"
