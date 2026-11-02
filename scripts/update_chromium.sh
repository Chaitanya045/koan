#!/usr/bin/env bash
# Moves the Chromium checkout to a release tag and re-applies Koan.
#
#   update_chromium.sh <version>           e.g. 158.0.8140.5 or 159.0.8200.1
#   update_chromium.sh --check [<version>] dry run, without touching anything:
#                                          will the patches apply? Against
#                                          <version> at googlesource, or, with
#                                          no version, against chromium/src.
#
# Koan is pinned to a release tag (docs/REBASE.md, "Following Chromium
# releases"), never to a main commit: tags are on a milestone's release
# branch (refs/branch-heads/<BUILD>). Steps: un-brand, reverse the hook
# patches, fetch the tag and check it out, gclient sync, record the pin
# (CHROMIUM_VERSION), re-apply patches (stops on conflicts), re-brand, list
# hook sites to re-audit. Rebuild afterwards with scripts/build.sh (a
# revision change rebuilds most of the tree). Never run while a build is
# running. The shared Linux checkout is switched under agent-build.sh's lock
# (.agent/notes/chromium-releases.md).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
# shellcheck source=scripts/koan_chromium.sh
source "$ROOT/scripts/koan_chromium.sh"
koan_require_env
SRC="$ROOT/chromium/src"
export PATH="$ROOT/depot_tools:$PATH"

audit() {
  echo
  echo "Koan hook sites in chromium/src (re-check each against the new code):"
  (cd "$SRC" && git grep -n -E 'koan::|koan/(browser|common)' -- \
    ':!koan' ':!*.gn' | sed 's/^/  /')
}

usage="usage: update_chromium.sh <version> | --check [<version>]"

if [[ ${1:-} == --check ]]; then
  if [[ -n ${2:-} ]]; then
    exec "$ROOT/scripts/ci/check_patches.sh" "$2"
  fi
  "$ROOT/scripts/apply_patches.sh" check
  audit
  exit 0
fi

version=${1:?$usage}
if ! koan_is_release_version "$version"; then
  echo "'$version' is not a release version like 158.0.8140.5." >&2
  echo "Koan is pinned to release tags, not to commits or branches." >&2
  exit 2
fi
rc=0
commit="$(koan_resolve_commit "$version")" || rc=$?
if [[ $rc == 1 ]]; then
  echo "Chromium has no tag $version" >&2
  exit 2
elif [[ $rc != 0 ]]; then
  echo "cannot reach chromium.googlesource.com; try again" >&2
  exit 3
fi

old="$(koan_pin_version)"
if [[ ${old%%.*} != "${version%%.*}" ]]; then
  echo "==> New milestone: M${old%%.*} -> M${version%%.*}. Follow the"
  echo "    'New milestone' checklist in docs/REBASE.md."
fi
echo "==> Chromium $old -> $version ($commit)"

# The files the patches touch, at the new version, before anything changes:
# a conflict now costs nothing.
"$ROOT/scripts/ci/check_patches.sh" "$commit" || {
  echo "Some patches don't apply to $version. Port them on a branch first" >&2
  echo "(docs/REBASE.md), then run this again." >&2
  exit 1
}

"$ROOT/scripts/apply_branding.sh" --revert
# Un-patch. What is applied in the checkout may be an older patch set than
# this branch's patches/ (the usual case on a rebase: the hooks were just
# ported), and `apply_patches.sh reverse` only reverses patches that match
# exactly. So put back every modified file that any known patch set owns:
# this branch's, and the ones recorded by agent-build.sh in
# $KOAN_PATCHED_FILES (default .cache/patched-files: every file any build
# ever patched). Anything else modified is not Koan's: stop.
patched_files="${KOAN_PATCHED_FILES:-$ROOT/.cache/patched-files}"
owned="$({ (cd "$ROOT/patches" && find . -name '*.patch' | sed -E 's#^\./##; s#\.patch$##')
           [[ ! -f $patched_files ]] || cat "$patched_files"; } | sort -u)"
modified="$(cd "$SRC" && git -c core.quotePath=false diff --name-only HEAD | sort -u)"
unowned="$(grep -vxF -f <(echo "$owned") <<<"$modified" || true)"
if [[ -n $unowned ]]; then
  echo "chromium/src has changes that are not Koan's patches or branding:" >&2
  echo "$unowned" | sed 's/^/  /' >&2
  exit 1
fi
if [[ -n $modified ]]; then
  (cd "$SRC" && git checkout -q -- .)
fi
if [[ -n "$(cd "$SRC" && git status --porcelain --untracked-files=no)" ]]; then
  echo "chromium/src still has local changes after un-patching; aborting" >&2
  exit 1
fi

# The tag, shallow: no history, and no other tag or branch head.
(cd "$SRC" && git fetch --depth=1 origin "refs/tags/$version" &&
  git checkout --detach FETCH_HEAD)
if [[ "$(git -C "$SRC" rev-parse HEAD)" != "$commit" ]]; then
  echo "chromium/src is not at $commit after the checkout; aborting" >&2
  exit 1
fi
# --revision: a managed .gclient (no "managed": False; the one gclient config
# writes) lets gclient move src to the tip of main without it.
(cd "$ROOT/chromium" && gclient sync --nohooks --no-history -D \
  --revision "src@$commit" && gclient runhooks)
if [[ "$(git -C "$SRC" rev-parse HEAD)" != "$commit" ]]; then
  echo "chromium/src is not at $commit after gclient sync; aborting" >&2
  exit 1
fi

# Pin the new revision for CI (scripts/ci/check_patches.sh), the Mac build
# (scripts/bootstrap.sh) and reviewers.
printf '%s\n%s\n' "$version" "$commit" > "$ROOT/CHROMIUM_VERSION"

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
