# shellcheck shell=bash
# The macOS CI runner's version of the one-folder env.sh
# (.agent/notes/mac.md): HOME, TMPDIR and the tool caches go into
# $ROOT/.cache/mac, so Koan's scripts accept the runner, and the caches
# (vpython, CIPD) travel with the source archive to every build job.
# Source, don't run. Used by .github/workflows/macos*.yml.
#
# DEVELOPER_DIR is the Xcode that scripts/ci/mac_runner_setup.sh kept
# (KOAN_CI_XCODE, Xcode 27.1 by default: SDK 27.0, the same Xcode as the
# maintainer's Mac).

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export KOAN_MAC="$ROOT/.cache/mac"
export HOME="$KOAN_MAC/home"
export TMPDIR="$KOAN_MAC/tmp"
export XDG_CACHE_HOME="$KOAN_MAC/home/.cache"
export XDG_CONFIG_HOME="$KOAN_MAC/home/.config"
export VPYTHON_VIRTUALENV_ROOT="$KOAN_MAC/home/.vpython-root"
export CIPD_CACHE_DIR="$KOAN_MAC/home/.cipd-cache"
export DEPOT_TOOLS_METRICS=0
# The depot_tools in the source archive stays as the fetch job left it.
export DEPOT_TOOLS_UPDATE=0
export DEVELOPER_DIR="${KOAN_CI_XCODE:-/Applications/Xcode_27.1.app}/Contents/Developer"
export PATH="$ROOT/depot_tools:$PATH"
mkdir -p "$HOME" "$TMPDIR"
# Koan's test-only update hooks (koan_update_test_hooks,
# src/browser/updates/BUILD.gn; scripts/build.sh adds them to args.gn): only
# in a run that is neither a release nor kept as a base for the nightlies.
# The chain's fetch job gets KOAN_CI_UPDATE_TEST_HOOKS (1 or 0) and leaves it
# in .cache, which travels to the build jobs in the source archive.
if [[ -n ${KOAN_CI_UPDATE_TEST_HOOKS:-} ]]; then
  mkdir -p "$ROOT/.cache"
  printf '%s\n' "$KOAN_CI_UPDATE_TEST_HOOKS" > "$ROOT/.cache/ci-update-test-hooks"
fi
if [[ "$(cat "$ROOT/.cache/ci-update-test-hooks" 2>/dev/null || true)" == 1 ]]; then
  export KOAN_UPDATE_TEST_HOOKS=1
else
  export KOAN_UPDATE_TEST_HOOKS=0
fi
