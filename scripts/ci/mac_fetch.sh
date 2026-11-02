#!/usr/bin/env bash
# macOS CI: gets the Chromium tree Koan builds from, ready to build.
#
#   scripts/ci/mac_fetch.sh
#
# 1. scripts/bootstrap.sh with a .gclient for target_os mac and
#    checkout_configuration "small" (no benchmark or GPU test data): a
#    shallow (--no-history) checkout of the pinned revision
#    (CHROMIUM_VERSION, line 2, whatever it is: a main commit or a release
#    tag's commit) and its hooks (clang, Rust, node, ...). Nothing here
#    names a Chromium version; changing CHROMIUM_VERSION is enough.
# 2. scripts/ci/prune_chromium_source.py: drops what the "lite" tarball
#    drops (test data, web tests, other platforms), so the source archive
#    every build job downloads stays small.
# 3. scripts/apply_patches.sh and scripts/apply_branding.sh.
# 4. Deletes Chromium's .git directories (several GB), as the tarballs do:
#    the build doesn't need them once the hooks (LASTCHANGE) have run. This
#    comes before gn gen, because some build files check for .git at gen
#    time (ANGLE's commit id) and would otherwise expect it in every job.
# 5. scripts/build.sh --gen: gn gen, so a broken build file fails here and
#    not hours into the build.
#
# Runs with scripts/ci/mac_env.sh sourced. Prints disk use as it goes.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=scripts/ci/mac_env.sh
source "$ROOT/scripts/ci/mac_env.sh"
cd "$ROOT"

step() { echo "==> $* ($(date -u +%H:%M:%S), $(df -h / | awk 'NR == 2 { print $4 }') free)"; }

mkdir -p chromium
cat > chromium/.gclient <<'EOF'
solutions = [{
  "name": "src",
  "url": "https://chromium.googlesource.com/chromium/src.git",
  "managed": False,
  "custom_deps": {},
  "custom_vars": {
    "checkout_configuration": "small",
  },
}]
target_os = ["mac"]
EOF

step "Fetching Chromium (scripts/bootstrap.sh)"
scripts/bootstrap.sh

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
du -sh chromium/src depot_tools "$KOAN_MAC" 2>/dev/null || true
