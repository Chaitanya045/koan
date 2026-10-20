#!/usr/bin/env bash
# Manage Koan's upstream hook patches in chromium/src.
#
# patches/<path>.patch holds the diff for chromium/src/<path>, one file each.
#
#   apply_patches.sh [apply]   apply every patch not yet applied
#   apply_patches.sh check     report each patch as applied / applicable / CONFLICT
#   apply_patches.sh reverse   revert every applied patch
#   apply_patches.sh export    rewrite patches/ from chromium/src's diff against
#                              HEAD (all modified tracked files except those
#                              owned by apply_branding.sh)
#
# Also (re)creates the chromium/src/koan -> ../../src symlink.
# Don't run while a build is running. Works on Linux and macOS (bash 3.2,
# BSD tools).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
koan_require_env
SRC="$ROOT/chromium/src"
PATCHES="$ROOT/patches"
MODE="${1:-apply}"

cd "$SRC"

link_koan() {
  if [[ -e koan && ! -L koan ]]; then
    echo "chromium/src/koan exists and is not a symlink; fix by hand" >&2
    exit 1
  fi
  if [[ "$(readlink koan || true)" != "../../src" ]]; then
    ln -sfn ../../src koan
    echo "linked //koan -> $ROOT/src"
  fi
  grep -qx '/koan' .git/info/exclude || echo '/koan' >> .git/info/exclude
}

patch_files() {
  # C collation: the same order with GNU and BSD sort.
  find "$PATCHES" -name '*.patch' -type f | LC_ALL=C sort
}

require_patches() {
  if [[ ! -d "$PATCHES" ]] || [[ -z "$(patch_files)" ]]; then
    echo "no patches in $PATCHES" >&2
    exit 1
  fi
}

is_applied() { git apply --reverse --check "$1" 2>/dev/null; }
can_apply() { git apply --check "$1" 2>/dev/null; }

case "$MODE" in
  apply)
    require_patches
    link_koan
    status=0
    while IFS= read -r p; do
      [[ -n "$p" ]] || continue
      rel="${p#"$PATCHES"/}"
      if is_applied "$p"; then
        echo "already applied  ${rel%.patch}"
      elif can_apply "$p" && git apply "$p"; then
        echo "applied          ${rel%.patch}"
      else
        echo "CONFLICT         ${rel%.patch}" >&2
        status=1
      fi
    done < <(patch_files)
    exit "$status"
    ;;
  check)
    require_patches
    status=0
    while IFS= read -r p; do
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
    while IFS= read -r p; do
      [[ -n "$p" ]] || continue
      if is_applied "$p"; then
        git apply --reverse "$p"
        rel="${p#"$PATCHES"/}"
        echo "reverted         ${rel%.patch}"
      fi
    done < <(patch_files)
    ;;
  export)
    # Diff against HEAD so staged changes are included. Write to a temp dir
    # and swap it in only if there is something to export.
    # Files owned by scripts/apply_branding.sh are rebranded by that script,
    # not stored as patches.
    files="$(git -c core.quotePath=false diff HEAD --name-only |
      grep -vxF -f <("$ROOT/scripts/apply_branding.sh" --list) || true)"
    if [[ -z "$files" ]]; then
      echo "chromium/src has no changes; keeping existing patches" >&2
      exit 1
    fi
    tmp="$(mktemp -d "$ROOT/.patches.XXXXXX")"
    while IFS= read -r f; do
      [[ -n "$f" ]] || continue
      mkdir -p "$tmp/$(dirname "$f")"
      git diff HEAD --no-color --full-index -- "$f" > "$tmp/$f.patch"
      echo "exported         $f"
    done <<< "$files"
    rm -rf "$PATCHES"
    mv "$tmp" "$PATCHES"
    ;;
  *)
    echo "usage: $0 [apply|check|reverse|export]" >&2
    exit 2
    ;;
esac
