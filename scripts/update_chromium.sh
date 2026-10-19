#!/usr/bin/env bash
# Moves the Chromium checkout to a new revision and re-applies Koan.
#
#   update_chromium.sh <git-ref>   e.g. 159.0.8150.0 or origin/main
#   update_chromium.sh --check     dry run: can the patches apply to HEAD?
#
# Steps: un-brand, reverse the hook patches, check out <git-ref>, gclient
# sync, re-apply patches (stops on conflicts), re-brand, list hook sites to
# re-audit. Rebuild afterwards with scripts/build.sh (a revision change
# rebuilds most of the tree). Never run while a build is running.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/chromium/src"
export PATH="$ROOT/depot_tools:$PATH"

audit() {
  echo
  echo "Koan hook sites in chromium/src (re-check each against the new code):"
  (cd "$SRC" && git grep -n -E 'koan::|koan/(browser|common)' -- \
    ':!koan' ':!*.gn' | sed 's/^/  /')
}

if [[ ${1:-} == --check ]]; then
  "$ROOT/scripts/apply_patches.sh" check
  audit
  exit 0
fi

ref=${1:?usage: update_chromium.sh <git-ref> | --check}

"$ROOT/scripts/apply_branding.sh" --revert
"$ROOT/scripts/apply_patches.sh" reverse
if [[ -n "$(cd "$SRC" && git status --porcelain --untracked-files=no)" ]]; then
  echo "chromium/src still has local changes after reversing Koan's; aborting" >&2
  exit 1
fi

(cd "$SRC" && git fetch --depth=1 origin "$ref" && git checkout --detach FETCH_HEAD)
(cd "$ROOT/chromium" && gclient sync --nohooks --no-history -D && gclient runhooks)

# Pin the new revision for CI (scripts/ci/check_patches.sh) and reviewers.
(cd "$SRC" && printf '%s\n%s\n' \
  "$(sed -n 's/^MAJOR=//p;s/^MINOR=//p;s/^BUILD=//p;s/^PATCH=//p' chrome/VERSION | paste -sd.)" \
  "$(git rev-parse HEAD)") > "$ROOT/CHROMIUM_VERSION"

if ! "$ROOT/scripts/apply_patches.sh" apply; then
  echo
  echo "Some patches conflict. Port each CONFLICT by hand in chromium/src, then"
  echo "run scripts/apply_patches.sh export and scripts/apply_branding.sh." >&2
  exit 1
fi
"$ROOT/scripts/apply_branding.sh"
audit
echo
echo "Now: scripts/build.sh, then run the checks in docs/REBASE.md."
