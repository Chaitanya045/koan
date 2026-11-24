#!/usr/bin/env bash
# Linux CI: finds the run to build on top of: the newest successful run of
# linux.yml in this repository, other than this one, that still has its
# source and its finished out/Release (koan-linux-source.tar.zst and
# koan-linux-out-final.tar.zst, kept 14 days by linux-chain.yml).
#
#   GH_TOKEN=... GH_REPO=owner/repo RUN=<this run id> scripts/ci/linux_find_base.sh
#
# Prints base_run=<id>, or base_run= when there is none, also to
# $GITHUB_OUTPUT when set. Whether that run can really be built on (same
# Chromium, same branding) is scripts/ci/linux_incremental.sh's call.
set -euo pipefail

: "${GH_REPO:?set GH_REPO}" "${RUN:?set RUN}"

output() { echo "base_run=$1"; [[ -z ${GITHUB_OUTPUT:-} ]] || echo "base_run=$1" >> "$GITHUB_OUTPUT"; }

# Filtered here, not with ?status=success: that filter has left out a
# successful run for hours after it finished.
runs="$(gh api "repos/$GH_REPO/actions/workflows/linux.yml/runs?per_page=50" \
  --jq '.workflow_runs[] | select(.conclusion == "success") | .id' 2>/dev/null || true)"
for id in $runs; do
  [[ $id != "$RUN" ]] || continue
  names="$(gh api "repos/$GH_REPO/actions/runs/$id/artifacts?per_page=100" \
    --jq '.artifacts[] | select(.expired | not) | .name' 2>/dev/null || true)"
  if grep -qx koan-linux-source.tar.zst <<<"$names" &&
      grep -qx koan-linux-out-final.tar.zst <<<"$names"; then
    output "$id"
    exit 0
  fi
done
echo "no earlier run with a source and a finished build: a full build"
output ""
