#!/usr/bin/env bash
# Linux CI: gets the Chromium tree Koan builds from, ready to build. The
# Linux twin of scripts/ci/mac_fetch.sh (its header has the reasons):
#
# 1. scripts/bootstrap.sh with a .gclient for target_os linux,
#    checkout_configuration "small", without clang-format's scripts; then
#    scripts/ci/mac_check_deps.py (not Mac-specific) fills any dependency
#    googlesource failed to serve from Chromium's lite tarball.
# 2. scripts/ci/prune_chromium_source.py (the lite tarball's lists, which
#    are what Linux distributions build from).
# 3. scripts/apply_patches.sh and scripts/apply_branding.sh.
# 4. Deletes Chromium's .git directories, before gn gen.
# 5. scripts/build.sh --gen with build/koan-release.gn.
#
# With KOAN_BASE_RUN=<run id> (scripts/ci/linux_find_base.sh), it first
# tries scripts/ci/linux_incremental.sh: that run's tree, brought up to this
# commit with only the changed files touched, so the build jobs rebuild
# only those on top of that run's out/Release. If that can't be done (new
# Chromium, new branding, expired archive), the workspace is wiped and the
# full fetch below runs. Writes based_on=<run id or empty> to
# $GITHUB_OUTPUT.
#
# Runs with scripts/ci/linux_env.sh sourced. Prints disk use as it goes.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=scripts/ci/linux_env.sh
source "$ROOT/scripts/ci/linux_env.sh"
cd "$ROOT"

step() { echo "==> $* ($(date -u +%H:%M:%S), $(df -h / | awk 'NR == 2 { print $4 }') free)"; }
output() { echo "$1=$2"; [[ -z ${GITHUB_OUTPUT:-} ]] || echo "$1=$2" >> "$GITHUB_OUTPUT"; }

if [[ -n ${KOAN_BASE_RUN:-} ]]; then
  step "Building on run $KOAN_BASE_RUN"
  status=0
  scripts/ci/linux_incremental.sh "$KOAN_BASE_RUN" || status=$?
  if ((status == 0)); then
    step "gn gen"
    JOBS=1 scripts/build.sh --gen
    output based_on "$KOAN_BASE_RUN"
    step "Done (incremental)"
    exit 0
  fi
  step "Can't build on run $KOAN_BASE_RUN (exit $status): a full fetch"
  rm -rf chromium depot_tools .cache
  git clean -ffdxq
  git reset -q --hard
  # shellcheck source=scripts/ci/linux_env.sh
  source "$ROOT/scripts/ci/linux_env.sh"
fi
output based_on ""

mkdir -p chromium
cat > chromium/.gclient <<'EOF'
solutions = [{
  "name": "src",
  "url": "https://chromium.googlesource.com/chromium/src.git",
  "managed": False,
  "custom_deps": {
    # git cl format's scripts, not used by the build; googlesource has
    # failed to serve them (scripts/ci/mac_fetch.sh).
    "src/third_party/clang-format/script": None,
    "src/third_party/webpagereplay/third_party/clang-format/script": None,
  },
  "custom_vars": {
    "checkout_configuration": "small",
  },
}]
target_os = ["linux"]
EOF

step "Fetching Chromium (scripts/bootstrap.sh)"
scripts/bootstrap.sh

step "Checking the dependencies"
python3 -I scripts/ci/mac_check_deps.py chromium "$(sed -n 1p CHROMIUM_VERSION)"

step "Pruning test data"
du -sh chromium/src
python3 -I scripts/ci/prune_chromium_source.py chromium/src
du -sh chromium/src

step "Applying patches and branding"
scripts/apply_patches.sh
scripts/apply_branding.sh

step "Deleting Chromium's .git directories"
du -sh chromium/src/.git
find chromium -name .git -prune -exec rm -rf {} +

step "gn gen"
JOBS=1 scripts/build.sh --gen

step "Done"
du -sh chromium/src depot_tools "$KOAN_LINUX_CACHE" 2>/dev/null || true
