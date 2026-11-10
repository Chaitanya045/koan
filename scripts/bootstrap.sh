#!/usr/bin/env bash
# Fetch depot_tools and a shallow Chromium checkout at the revision Koan is
# pinned to (CHROMIUM_VERSION, line 2: the commit of a release tag, which is
# on the milestone's release branch, not on main), for this platform only,
# then run the hooks. Works on Linux and macOS.
#
# Safe to re-run: depot_tools and .gclient are created only if missing, and
# the sync is skipped when chromium/src is already at the pinned revision.
# To move an existing checkout to another release, use update_chromium.sh.
# On macOS, run it with the folder's env.sh sourced (.agent/notes/mac.md).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
# shellcheck source=scripts/koan_chromium.sh
source "$ROOT/scripts/koan_chromium.sh"
koan_require_env
export PATH="$ROOT/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=1

OS="$(koan_os)"

VERSION="$(koan_pin_version)"
REV="$(koan_pin_commit)"
if ! koan_is_release_version "$VERSION"; then
  echo "CHROMIUM_VERSION line 1 must be a release version, got '$VERSION'" >&2
  exit 1
fi
if ! koan_is_commit "$REV"; then
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
  if [[ "$(git -C src rev-parse HEAD)" == "$REV" ]]; then
    # Koan's patches or branding may be applied; leave them be.
    echo "==> chromium/src is already at $VERSION; skipping gclient sync"
    sync=0
  elif [[ -n "$(git -C src status --porcelain --untracked-files=no)" ]]; then
    echo "chromium/src has local changes and is not at $REV." >&2
    echo "Revert them first: scripts/apply_branding.sh --revert &&" \
      "scripts/apply_patches.sh reverse" >&2
    exit 1
  fi
fi

if [[ $sync == 1 ]]; then
  echo "==> Syncing Chromium $VERSION @ $REV (shallow, no hooks)"
  # googlesource now and then answers a fetch with HTTP 500; gclient picks
  # up where it stopped, so try again.
  for attempt in 1 2 3; do
    gclient sync --nohooks --no-history --revision "src@$REV" && break
    if ((attempt == 3)); then exit 1; fi
    echo "gclient sync failed (attempt $attempt of 3); retrying in a minute" >&2
    sleep 60
  done
  if [[ "$(git -C src rev-parse HEAD)" != "$REV" ]]; then
    echo "chromium/src is not at $REV after the sync (CHROMIUM_VERSION)" >&2
    exit 1
  fi
fi

echo "==> Running hooks (toolchain, SDK checks, test data)"
gclient runhooks

echo "==> Bootstrap complete: Chromium $VERSION"
