# shellcheck shell=bash
# Chromium revisions for Koan's scripts. Source, don't run.
#
# Koan is pinned (CHROMIUM_VERSION) to a release tag of one milestone, never
# to a main commit: line 1 is the tag's version (158.0.8127.2), line 2 the
# commit it points to. Tags of a milestone live on its release branch
# (refs/branch-heads/<BUILD>, e.g. 8127 for 158.0.8127.x), not on main.
#
# Works with macOS's bash 3.2.

KOAN_GITILES=https://chromium.googlesource.com/chromium/src

# koan_is_release_version <string>: is it a MAJOR.MINOR.BUILD.PATCH version?
koan_is_release_version() { [[ $1 =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; }

# koan_is_commit <string>: is it a full commit hash?
koan_is_commit() { [[ $1 =~ ^[0-9a-f]{40}$ ]]; }

# koan_pin_version / koan_pin_commit: the two lines of CHROMIUM_VERSION.
# Needs ROOT.
koan_pin_version() { sed -n 1p "$ROOT/CHROMIUM_VERSION"; }
koan_pin_commit() { sed -n 2p "$ROOT/CHROMIUM_VERSION"; }

# koan_resolve_commit <version|commit|ref>: prints the full commit hash at
# chromium.googlesource.com. A version is looked up as a release tag; a
# branch-head can be given as branch-heads/8127 (or refs/branch-heads/8127).
# Returns 1 if there is no such revision.
koan_resolve_commit() {
  local ref=$1 json
  if koan_is_commit "$ref"; then
    echo "$ref"
    return
  fi
  if koan_is_release_version "$ref"; then
    ref=refs/tags/$ref
  elif [[ $ref == branch-heads/* || $ref == tags/* ]]; then
    ref=refs/$ref
  fi
  # Gitiles puts a line before its JSON so it can't be run as a script.
  json="$(curl -fsSL --retry 6 --retry-delay 10 --retry-max-time 180 \
    "$KOAN_GITILES/+/$ref?format=JSON")" || return 1
  json="$(sed -n 's/^  "commit": "\([0-9a-f]\{40\}\)".*/\1/p' <<<"$json" | head -n 1)"
  [[ -n $json ]] || return 1
  echo "$json"
}
