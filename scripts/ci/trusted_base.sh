# shellcheck shell=bash
# CI: which runs may be a base for incremental builds, shared by
# scripts/ci/find_base.sh (which picks one) and scripts/ci/prune_bases.sh
# (which deletes old ones). Source, don't run.
#
# A base is a trusted build: one whose code is merged, so its objects may be
# linked into what users install. Two kinds of run are, and nothing else
# (no pull request, no other branch, no ref input):
#   - WORKFLOW's runs on BRANCH (develop) itself, from a schedule or
#     workflow_dispatch (the nightlies; macos.yml or linux.yml);
#   - RELEASE_WORKFLOW's runs (release.yml; empty for none), from a pushed
#     v* tag or a workflow_dispatch, either from BRANCH or from that tag.
#     A release builds the tag's commit, and only org admins may create v*
#     tags (the "release tags (v*)" ruleset); release.yml checks that it is
#     on develop or a release/* branch. But a dispatch runs the workflow
#     files of the ref it is started from, and a *branch* may be called
#     v-something too: so a run whose head_branch is a v* name only counts
#     when that tag exists and points at the run's head_sha, i.e. the
#     workflow files that ran are the tag's own.
# Only such runs upload <prefix>-out-final.tar.zst (keep_base), which is the
# second lock: both have to fail before unmerged code becomes a base.
#
# Needs WORKFLOW, BRANCH, RELEASE_WORKFLOW and GH_REPO.

# trusted_base_run <workflow file> <event> <head_branch> <head_sha>
# Succeeds when a run with these is a trusted build (above).
trusted_base_run() {
  local workflow="$1" event="$2" branch="$3" sha="$4" ref type target
  if [[ $workflow == "$WORKFLOW" ]]; then
    [[ $branch == "$BRANCH" && ($event == schedule || $event == workflow_dispatch) ]]
    return
  fi
  [[ -n $RELEASE_WORKFLOW && $workflow == "$RELEASE_WORKFLOW" ]] || return 1
  [[ $event == push || $event == workflow_dispatch ]] || return 1
  if [[ $event == workflow_dispatch && $branch == "$BRANCH" ]]; then
    return 0
  fi
  # A tag name as release.yml accepts it; then the tag's commit (an
  # annotated tag's too) must be the run's. git/ref/ matches the name
  # exactly (git/refs/ would take a prefix).
  [[ $branch =~ ^v[0-9A-Za-z.-]+$ && $sha =~ ^[0-9a-f]{40}$ ]] || return 1
  ref="$(gh api "repos/$GH_REPO/git/ref/tags/$branch" \
    --jq '.object.type + " " + .object.sha' 2>/dev/null)" || return 1
  read -r type target <<<"$ref"
  if [[ $type == tag ]]; then
    ref="$(gh api "repos/$GH_REPO/git/tags/$target" \
      --jq '.object.type + " " + .object.sha' 2>/dev/null)" || return 1
    read -r type target <<<"$ref"
  fi
  [[ $type == commit && $target == "$sha" ]]
}
