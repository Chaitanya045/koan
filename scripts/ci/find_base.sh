#!/usr/bin/env bash
# CI: finds the run an incremental build starts from: the newest successful
# trusted run in this repository, other than this one, that still has its
# source and its finished build (<prefix>-source.tar.zst and
# <prefix>-out-final.tar.zst).
#
#   GH_TOKEN=... GH_REPO=owner/repo RUN=<this run id> BRANCH=develop \
#     [RELEASE_WORKFLOW=release.yml] \
#     scripts/ci/find_base.sh <workflow file> <artifact prefix>
#   e.g. scripts/ci/find_base.sh macos.yml koan-mac
#
# Trusted runs (scripts/ci/trusted_base.sh) are of two kinds: the platform's
# nightly workflow (<workflow file>) on BRANCH (default develop) as a
# schedule or workflow_dispatch event, and the release workflow's
# (RELEASE_WORKFLOW, default release.yml; empty for none) for a v* tag. The
# newest of either kind wins (created_at), so the first build after a full
# one, nightly or release, builds on it, and so does every later one.
# Pull-request runs and other branches never are, and the chains only
# upload <prefix>-out-final.tar.zst for runs that built develop or a release
# tag (keep_base), so objects built from unmerged code never get linked
# into what users install. The prefix keeps the platforms apart: a release
# run has both koan-mac-* and koan-linux-* archives.
#
# Prints base_run=<id>, or base_run= when there is none, also to
# $GITHUB_OUTPUT when set. Whether that run can really be built on (same
# Chromium, fetch inputs and toolchain) is scripts/ci/incremental.sh's call.
set -euo pipefail

WORKFLOW="${1:?usage: $0 <workflow file> <artifact prefix>}"
PREFIX="${2:?usage: $0 <workflow file> <artifact prefix>}"
: "${GH_REPO:?set GH_REPO}" "${RUN:?set RUN}"
BRANCH="${BRANCH:-develop}"
RELEASE_WORKFLOW="${RELEASE_WORKFLOW-release.yml}"
# shellcheck source=scripts/ci/trusted_base.sh
source "$(dirname "$0")/trusted_base.sh"

output() { echo "base_run=$1"; [[ -z ${GITHUB_OUTPUT:-} ]] || echo "base_run=$1" >> "$GITHUB_OUTPUT"; }

# Only runs that can be a base are asked for, one request per workflow and
# event, newest first: BRANCH's schedule and workflow_dispatch runs of
# WORKFLOW (branch= and event=), and RELEASE_WORKFLOW's push and
# workflow_dispatch runs (event= only: a tag push's head_branch is the tag).
# Every label added to any pull request starts a run of the nightly
# workflows (most are skipped at once), so the newest 50 runs of any kind
# pushed the last develop run off the page (2026-11-27: full Linux builds
# for #469, #470 and #519 although develop's base was there; #537). event=
# keeps develop's other runs from doing the same. Up to PAGES pages of 100
# per request; a base is kept 14 days, so the first page is nearly always
# enough.
# The conclusion is still filtered here, not with ?status=success: that
# filter has left out a successful run (tnfssc/koan 45281812693) for hours
# after it finished. The branch and event are checked again too
# (trusted_base_run).
PAGES="${PAGES:-3}"
list() {
  local workflow="$1" query="$2" page got
  for ((page = 1; page <= PAGES; page++)); do
    got="$(gh api "repos/$GH_REPO/actions/workflows/$workflow/runs?${query}per_page=100&page=$page" \
      --jq ".workflow_runs[] | [.created_at, .id, (.conclusion // \"none\"), .event, (.head_branch // \"-\"), (.head_sha // \"-\"), \"$workflow\"] | @tsv" \
      2>/dev/null || true)"
    [[ -n $got ]] || break
    echo "$got"
    (( $(wc -l <<<"$got") == 100 )) || break
  done
}
candidates() {
  local event
  for event in schedule workflow_dispatch; do
    list "$WORKFLOW" "branch=$BRANCH&event=$event&"
  done
  if [[ -n $RELEASE_WORKFLOW ]]; then
    for event in push workflow_dispatch; do
      list "$RELEASE_WORKFLOW" "event=$event&"
    done
  fi
}
# All of them merged, newest first (created_at is ISO 8601 UTC). No field
# is ever empty (read would merge two tabs and shift the rest).
while IFS=$'\t' read -r _ id conclusion event branch sha workflow; do
  [[ $id != "$RUN" && $conclusion == success ]] || continue
  if ! trusted_base_run "$workflow" "$event" "$branch" "$sha"; then
    continue
  fi
  names="$(gh api "repos/$GH_REPO/actions/runs/$id/artifacts?per_page=100" \
    --jq '.artifacts[] | select(.expired | not) | .name' 2>/dev/null || true)"
  if grep -qx "$PREFIX-source.tar.zst" <<<"$names" &&
      grep -qx "$PREFIX-out-final.tar.zst" <<<"$names"; then
    echo "run $id ($workflow, $event, $branch) is the base"
    output "$id"
    exit 0
  fi
done < <(candidates | sort -r | awk -F '\t' '!seen[$2]++')
echo "no earlier trusted run with a source and a finished build: a full build"
output ""
