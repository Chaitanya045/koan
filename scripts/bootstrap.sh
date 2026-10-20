#!/usr/bin/env bash
# Fetch depot_tools and a shallow Chromium checkout at the revision Koan is
# pinned to (CHROMIUM_VERSION, line 2), for this platform only, then run
# the hooks. Works on Linux and macOS.
#
# Safe to re-run: depot_tools and .gclient are created only if missing, and
# the sync is skipped when chromium/src is already at the pinned revision
# with Koan's patches or branding applied (they would get in its way).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
export PATH="$ROOT/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=1

OS="$(koan_os)"
[[ $OS == mac ]] && koan_require_xcode

VERSION="$(sed -n 1p "$ROOT/CHROMIUM_VERSION")"
REV="$(sed -n 2p "$ROOT/CHROMIUM_VERSION")"
if ! [[ $REV =~ ^[0-9a-f]{40}$ ]]; then
  echo "CHROMIUM_VERSION line 2 must be a full commit hash, got '$REV'" >&2
  exit 1
fi

if [[ ! -d "$ROOT/depot_tools" ]]; then
  echo "==> Cloning depot_tools"
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$ROOT/depot_tools"
fi

mkdir -p "$ROOT/chromium"
cd "$ROOT/chromium"

# Only this platform's dependencies (target_os), unmanaged so gclient leaves
# the revision of src to --revision below.
if [[ ! -f .gclient ]]; then
  echo "==> Writing .gclient (target_os $OS)"
  cat > .gclient <<EOF
solutions = [{
  "name": "src",
  "url": "https://chromium.googlesource.com/chromium/src.git",
  "managed": False,
  "custom_deps": {},
  "custom_vars": {},
}]
target_os = ["$OS"]
EOF
fi

sync=1
if [[ -d src/.git ]]; then
  head="$(git -C src rev-parse HEAD)"
  dirty="$(git -C src status --porcelain --untracked-files=no)"
  if [[ -n $dirty ]]; then
    if [[ $head == "$REV" ]]; then
      echo "==> chromium/src is at $VERSION with local changes (Koan's patches"
      echo "    or branding); skipping gclient sync"
      sync=0
    else
      echo "chromium/src has local changes and is not at $REV." >&2
      echo "Revert them first: scripts/apply_branding.sh --revert &&" \
        "scripts/apply_patches.sh reverse" >&2
      exit 1
    fi
  fi
fi

if [[ $sync == 1 ]]; then
  echo "==> Syncing Chromium $VERSION @ $REV (shallow, no hooks)"
  gclient sync --nohooks --no-history --revision "src@$REV"
fi

echo "==> Running hooks (toolchain, SDK checks, test data)"
gclient runhooks

echo "==> Bootstrap complete: Chromium $VERSION"
