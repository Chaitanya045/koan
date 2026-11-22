#!/usr/bin/env bash
# Linux CI: turns an earlier run's source tree into this commit's, touching
# only what changed, so the build on top of that run's out/Release
# rebuilds only that.
#
#   GH_TOKEN=... GH_REPO=owner/repo scripts/ci/linux_incremental.sh <base run id>
#
# KOAN_BASE_ARCHIVE=<file> takes the base's source archive from a file
# instead (tests).
#
# siso decides what to rebuild by mtime, and a fresh checkout gives every
# file a new one, so reusing out/Release alone would rebuild everything.
# Instead this downloads the base run's source archive (the patched,
# branded Chromium tree, depot_tools and caches, with their mtimes) and:
#
# 1. unpacks it over the workspace (this commit's checkout);
# 2. gives up (exit 3) if CHROMIUM_VERSION or the branding inputs differ:
#    the tree has no .git to undo branding with, and a new Chromium is a
#    full build anyway;
# 3. copies this commit's files over the base's with rsync --checksum, so
#    only files whose content changed get a new mtime;
# 4. for each patch that changed, reverses the base's version and applies
#    this commit's with GNU patch (no .git in the tree).
#
# Exits 0 when the tree is this commit's; anything else leaves a mixed tree
# that linux_fetch.sh wipes before a full fetch. Run from linux_fetch.sh,
# with scripts/ci/linux_env.sh sourced and KOAN_SCRATCH set.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BASE="${1:?usage: $0 <base run id>}"
: "${GH_REPO:?set GH_REPO}" "${KOAN_SCRATCH:?set KOAN_SCRATCH}"
cd "$ROOT"

step() { echo "==> $* ($(date -u +%H:%M:%S))"; }

archive="$KOAN_SCRATCH/base-source.tar.zst"
if [[ -n ${KOAN_BASE_ARCHIVE:-} ]]; then
  # Tests: a local archive instead of the run's artifact.
  cp "$KOAN_BASE_ARCHIVE" "$archive"
else
  step "Downloading the source of run $BASE"
  id="$(gh api "repos/$GH_REPO/actions/runs/$BASE/artifacts?per_page=100" \
    --jq '.artifacts[] | select(.name == "koan-linux-source.tar.zst" and (.expired | not)) | .id')"
  [[ -n $id ]] || { echo "run $BASE has no source archive any more" >&2; exit 3; }
  # An artifact uploaded with archive: false comes back as the file itself.
  gh api "repos/$GH_REPO/actions/artifacts/$id/zip" > "$archive"
fi
ls -l "$archive"

new="$KOAN_SCRATCH/koan-new"
old_patches="$KOAN_SCRATCH/base-patches"
rm -rf "$new" "$old_patches"
mkdir -p "$new"
git archive HEAD | tar -x -C "$new"

step "Unpacking it"
scripts/ci/linux_archive.sh unpack "$archive"

if ! cmp -s CHROMIUM_VERSION "$new/CHROMIUM_VERSION"; then
  echo "CHROMIUM_VERSION moved: a full build" >&2
  exit 3
fi
for p in branding scripts/branding.py scripts/apply_branding.sh; do
  if ! diff -rq --exclude=out "$p" "$new/$p" >/dev/null; then
    echo "$p changed since run $BASE: a full build" >&2
    exit 3
  fi
done
cp -a patches "$old_patches"

step "Bringing in this commit's changes"
# No -t: a file whose content changes gets a new mtime, the others keep the
# base's. The .gitignore filter keeps (and so doesn't delete) the ignored
# trees: chromium/, depot_tools/, .cache/, branding/out/.
rsync -rlcE --delete --itemize-changes --filter=':- .gitignore' --exclude=/.git \
  "$new/" ./ > "$KOAN_SCRATCH/rsync.log"
grep -v '^\.' "$KOAN_SCRATCH/rsync.log" || echo "(no file changed)"

step "Bringing Koan's patches up to date"
changed=0
while IFS= read -r rel; do
  [[ -n $rel ]] || continue
  o="$old_patches/$rel" n="patches/$rel"
  if [[ -f $o && -f $n ]] && cmp -s "$o" "$n"; then continue; fi
  if [[ -f $o ]]; then
    patch -d chromium/src -p1 -R -s --dry-run < "$o" >/dev/null ||
      { echo "can't reverse the base's $rel" >&2; exit 3; }
    patch -d chromium/src -p1 -R -s --no-backup-if-mismatch < "$o"
  fi
  if [[ -f $n ]]; then
    patch -d chromium/src -p1 -N -s --dry-run < "$n" >/dev/null ||
      { echo "can't apply $rel" >&2; exit 3; }
    patch -d chromium/src -p1 -N -s --no-backup-if-mismatch < "$n"
  fi
  echo "updated ${rel%.patch}"
  changed=$((changed + 1))
done < <({ (cd "$old_patches" && find . -name '*.patch'); (cd patches && find . -name '*.patch'); } |
  sed 's#^\./##' | LC_ALL=C sort -u)
echo "$changed patches changed"
rm -rf "$new" "$old_patches"
