# shellcheck shell=bash
# The Linux CI runner's environment for Koan's scripts: depot_tools on PATH,
# no depot_tools self-update, and the tool caches (vpython, CIPD) in
# $ROOT/.cache/linux, so they travel with the source archive to every build
# job like the macOS chain's .cache/mac. HOME stays the runner's own: Linux
# builds don't need the macOS one-folder rule.
# Source, don't run. Used by .github/workflows/linux*.yml.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export KOAN_LINUX_CACHE="$ROOT/.cache/linux"
export XDG_CACHE_HOME="$KOAN_LINUX_CACHE/cache"
export VPYTHON_VIRTUALENV_ROOT="$KOAN_LINUX_CACHE/vpython-root"
export CIPD_CACHE_DIR="$KOAN_LINUX_CACHE/cipd-cache"
export DEPOT_TOOLS_METRICS=0
# The depot_tools in the source archive stays as the fetch job left it.
export DEPOT_TOOLS_UPDATE=0
# The release build (build/koan-release.gn, out/Release): one binary, no
# DCHECKs, what gets packaged.
export KOAN_CONFIG=release
export PATH="$ROOT/depot_tools:$PATH"
mkdir -p "$XDG_CACHE_HOME"
