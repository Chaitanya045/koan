#!/usr/bin/env bash
# CI: finds the run an incremental build starts from: the newest successful
# run of the platform's nightly workflow in this repository, other than this
# one, that still has its source and its finished build
# (<prefix>-source.tar.zst and <prefix>-out-final.tar.zst).
#
#   GH_TOKEN=... GH_REPO=owner/repo RUN=<this run id> BRANCH=develop \
#     scripts/ci/find_base.sh <workflow file> <artifact prefix>
#   e.g. scripts/ci/find_base.sh macos.yml koan-mac
#
# Only a trusted run is a base: one that ran on BRANCH (default develop) as
# a schedule or workflow_dispatch event. Pull-request runs never are, and
# the workflow only uploads <prefix>-out-final.tar.zst for runs that built
# BRANCH itself (keep_base), so objects built from unmerged code never get
# linked into what users install.
#
# Prints base_run=<id>, or base_run= when there is none, also to
# $GITHUB_OUTPUT when set. Whether that run can really be built on (same
# Chromium, fetch inputs and toolchain) is scripts/ci/incremental.sh's call.
set -euo pipefail

WORKFLOW="${1:?usage: $0 <workflow file> <artifact prefix>}"
PREFIX="${2:?usage: $0 <workflow file> <artifact prefix>}"
: "${GH_REPO:?set GH_REPO}" "${RUN:?set RUN}"
export BRANCH="${BRANCH:-develop}"

output() { echo "base_run=$1"; [[ -z ${GITHUB_OUTPUT:-} ]] || echo "base_run=$1" >> "$GITHUB_OUTPUT"; }

# Filtered here, not with ?status=success: that filter has left out a
# successful run (tnfssc/koan 45281812693) for hours after it finished.
runs="$(gh api "repos/$GH_REPO/actions/workflows/$WORKFLOW/runs?per_page=50" \
  --jq '.workflow_runs[] | select(.conclusion == "success" and .head_branch == env.BRANCH
    and (.event == "schedule" or .event == "workflow_dispatch")) | .id' 2>/dev/null || true)"
for id in $runs; do
  [[ $id != "$RUN" ]] || continue
  names="$(gh api "repos/$GH_REPO/actions/runs/$id/artifacts?per_page=100" \
    --jq '.artifacts[] | select(.expired | not) | .name' 2>/dev/null || true)"
  if grep -qx "$PREFIX-source.tar.zst" <<<"$names" &&
      grep -qx "$PREFIX-out-final.tar.zst" <<<"$names"; then
    output "$id"
    exit 0
  fi
done
echo "no earlier $BRANCH run with a source and a finished build: a full build"
output ""
