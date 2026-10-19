#!/usr/bin/env bash
# Manage Koan's upstream hook patches in chromium/src.
#
# patches/<path>.patch holds the diff for chromium/src/<path>, one file each.
#
#   apply_patches.sh [apply]   apply every patch not yet applied
#   apply_patches.sh check     report each patch as applied / applicable / CONFLICT
#   apply_patches.sh reverse   revert every applied patch
#   apply_patches.sh export    rewrite patches/ from the working tree diff of
#                              chromium/src (all modified tracked files)
#
# Also (re)creates the chromium/src/koan -> ../../src symlink.
# Don't run while a build is running.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/chromium/src"
PATCHES="$ROOT/patches"
MODE="${1:-apply}"

cd "$SRC"

link_koan() {
  if [[ ! -L koan ]]; then
    ln -sfn ../../src koan
    echo "linked //koan -> $ROOT/src"
  fi
  grep -qx '/koan' .git/info/exclude || echo '/koan' >> .git/info/exclude
}

patch_files() {
  find "$PATCHES" -name '*.patch' -type f | sort
}

is_applied() { git apply --reverse --check "$1" 2>/dev/null; }
can_apply() { git apply --check "$1" 2>/dev/null; }

case "$MODE" in
  apply)
    link_koan
    status=0
    while read -r p; do
      [[ -n "$p" ]] || continue
      rel="${p#"$PATCHES"/}"
      if is_applied "$p"; then
        echo "already applied  ${rel%.patch}"
      elif can_apply "$p"; then
        git apply "$p"
        echo "applied          ${rel%.patch}"
      else
        echo "CONFLICT         ${rel%.patch}" >&2
        status=1
      fi
    done < <(patch_files)
    exit "$status"
    ;;
  check)
    status=0
    while read -r p; do
      [[ -n "$p" ]] || continue
      rel="${p#"$PATCHES"/}"
      if is_applied "$p"; then
        echo "applied          ${rel%.patch}"
      elif can_apply "$p"; then
        echo "applicable       ${rel%.patch}"
      else
        echo "CONFLICT         ${rel%.patch}"
        status=1
      fi
    done < <(patch_files)
    exit "$status"
    ;;
  reverse)
    while read -r p; do
      [[ -n "$p" ]] || continue
      if is_applied "$p"; then
        git apply --reverse "$p"
        rel="${p#"$PATCHES"/}"
        echo "reverted         ${rel%.patch}"
      fi
    done < <(patch_files)
    ;;
  export)
    rm -rf "$PATCHES"
    mkdir -p "$PATCHES"
    while read -r f; do
      [[ -n "$f" ]] || continue
      mkdir -p "$PATCHES/$(dirname "$f")"
      git diff --no-color --full-index -- "$f" > "$PATCHES/$f.patch"
      echo "exported         $f"
    done < <(git diff --name-only)
    ;;
  *)
    echo "usage: $0 [apply|check|reverse|export]" >&2
    exit 2
    ;;
esac
