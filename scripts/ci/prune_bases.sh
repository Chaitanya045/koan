#!/usr/bin/env bash
# CI: the cleanup of a keep_base run that succeeded: deletes the source and
# finished build (<prefix>-source.tar.zst, <prefix>-out-final.tar.zst) of
# older runs of the workflow on BRANCH, except those another run may still
# be building on.
#
#   GH_TOKEN=... GH_REPO=owner/repo RUN=<this run id> BRANCH=develop \
#     scripts/ci/prune_bases.sh <workflow file> <artifact prefix>
#   e.g. scripts/ci/prune_bases.sh linux.yml koan-linux
#
# A run picks its base (scripts/ci/find_base.sh) when its fetch job starts,
# but its build 1 only downloads the base's out-final once the fetch is done
# (up to the fetch's 5 h timeout, plus waiting for a runner). Deleting the
# base in between silently turned that build into a full one (#569). So a
# base is kept until a newer base has existed for GRACE_HOURS (default 12):
# from then on, every run's find_base.sh has picked the newer one. A base
# "exists" from its run's end (updated_at of the completed run, a little
# after find_base.sh can see it), and this run's own from now. With one
# develop build a day that means the previous base goes the next day; with
# several in a day, the ones superseded in the last 12 h stay until a later
# run's cleanup.
#
# Older runs that are no base (failed, cancelled, or missing one of the two
# archives) lose both archives at once, as before; runs still in progress
# keep theirs (a running chain needs its source). Only runs of WORKFLOW on
# BRANCH are touched: release.yml's and pull requests' archives are theirs.
#
# NOW=<epoch seconds> stands in for the clock (tests).
set -euo pipefail

WORKFLOW="${1:?usage: $0 <workflow file> <artifact prefix>}"
PREFIX="${2:?usage: $0 <workflow file> <artifact prefix>}"
: "${GH_REPO:?set GH_REPO}" "${RUN:?set RUN}"
BRANCH="${BRANCH:-develop}"
GRACE_HOURS="${GRACE_HOURS:-12}"
NOW="${NOW:-$(date +%s)}"
grace=$((GRACE_HOURS * 3600))

# Unexpired archives of BRANCH's runs older than this one: run id ->
# artifact ids, and which of the two each run has.
declare -A ids=() has=()
for name in "$PREFIX-source.tar.zst" "$PREFIX-out-final.tar.zst"; do
  while IFS=$'\t' read -r id run; do
    if [[ -z $id ]] || ((run >= RUN)); then continue; fi
    ids[$run]+=" $id"
    has[$run]+=" $name"
  done < <(gh api --paginate "repos/$GH_REPO/actions/artifacts?name=$name&per_page=100" \
    --jq ".artifacts[] | select((.expired | not) and .workflow_run.head_branch == \"$BRANCH\") | [.id, .workflow_run.id] | @tsv")
done

delete() {
  local run="$1" id
  shift
  echo "deleting the archives of run $run ($*)"
  for id in ${ids[$run]}; do
    gh api -X DELETE "repos/$GH_REPO/actions/artifacts/$id"
  done
}

# Newest first: each base is superseded from when the base next above it
# appeared (this run: now).
superseded=$NOW
for run in $(printf '%s\n' "${!ids[@]}" | sort -rn); do
  read -r status conclusion updated path < <(gh api "repos/$GH_REPO/actions/runs/$run" \
    --jq '[.status, (.conclusion // "none"), (.updated_at | fromdateiso8601), (.path | split("@")[0] | split("/") | last)] | @tsv') || continue
  [[ $path == "$WORKFLOW" ]] || continue
  if [[ $status != completed ]]; then
    echo "keeping the archives of run $run: still $status"
    continue
  fi
  if [[ $conclusion != success || ${has[$run]} != *-source.tar.zst* || ${has[$run]} != *-out-final.tar.zst* ]]; then
    delete "$run" "no base: $conclusion,${has[$run]}"
    continue
  fi
  age=$((NOW - superseded))
  if ((age < grace)); then
    echo "keeping the base of run $run: a newer one exists for $((age / 60)) min only (under ${GRACE_HOURS} h), so a build may still be using it"
  else
    delete "$run" "a newer base exists for $((age / 3600)) h"
  fi
  ((updated >= superseded)) || superseded=$updated
done
