#!/usr/bin/env bash
# Fetch depot_tools and a shallow Chromium checkout, then run hooks.
# Safe to re-run: each step is skipped if already done.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="$ROOT/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=1

if [[ ! -d "$ROOT/depot_tools" ]]; then
  echo "==> Cloning depot_tools"
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$ROOT/depot_tools"
fi

mkdir -p "$ROOT/chromium"
cd "$ROOT/chromium"

if [[ ! -d src/.git ]]; then
  echo "==> Fetching Chromium (shallow, no hooks)"
  fetch --nohooks --no-history chromium
else
  echo "==> Syncing existing checkout"
  gclient sync --nohooks --no-history
fi

echo "==> Running hooks (toolchain, sysroot, test data)"
gclient runhooks

echo "==> Bootstrap complete"
