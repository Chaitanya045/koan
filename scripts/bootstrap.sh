#!/usr/bin/env bash
# Fetch depot_tools and a shallow Chromium checkout at the revision Koan is
# pinned to (CHROMIUM_VERSION, line 2: the commit of a release tag, which is
# on the milestone's release branch, not on main), for this platform only,
# then run the hooks. Works on Linux and macOS.
#
# Safe to re-run, and resumes after an interruption: depot_tools and
# .gclient are created only if missing, and the sync is skipped only once a
# sync of the pinned revision has finished (.cache/build-state/synced); an
# interrupted sync runs again, and gclient picks up where it stopped.
# To move an existing checkout to another release, use update_chromium.sh
# (scripts/build.sh on a Mac does that by itself after a git pull).
#
# On macOS the folder's env.sh may be sourced (.agent/notes/mac.md); without
# it the tool caches go into .cache/mac in the clone. Friends normally run
# scripts/build.sh, which runs this when needed (docs/BUILDING-MAC.md).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
# shellcheck source=scripts/koan_chromium.sh
source "$ROOT/scripts/koan_chromium.sh"
koan_require_env
export PATH="$ROOT/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE="${DEPOT_TOOLS_UPDATE:-1}"

OS="$(koan_os)"
STATE="$ROOT/.cache/build-state"
mkdir -p "$STATE"

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

if [[ ! -d "$ROOT/depot_tools/.git" ]]; then
  # An interrupted clone leaves a partial directory: start it again.
  rm -rf "$ROOT/depot_tools"
  echo "==> Cloning depot_tools"
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$ROOT/depot_tools"
fi

mkdir -p "$ROOT/chromium"
cd "$ROOT/chromium"

# Only this platform's dependencies (target_os), unmanaged so gclient leaves
# the revision of src to --revision below. On macOS, as in CI
# (scripts/ci/mac_fetch.sh): no benchmark or GPU test data, and not
# clang-format's scripts (git cl format only), which googlesource has failed
# to serve.
if [[ ! -f .gclient ]]; then
  echo "==> Writing .gclient (target_os $OS)"
  deps='{}'
  vars='{}'
  if [[ $OS == mac ]]; then
    deps='{
    "src/third_party/clang-format/script": None,
    "src/third_party/webpagereplay/third_party/clang-format/script": None,
  }'
    vars='{"checkout_configuration": "small"}'
  fi
  cat > .gclient <<EOF
solutions = [{
  "name": "src",
  "url": "https://chromium.googlesource.com/chromium/src.git",
  "managed": False,
  "custom_deps": $deps,
  "custom_vars": $vars,
}]
target_os = ["$OS"]
EOF
fi

sync=1
if [[ -d src/.git ]]; then
  head="$(git -C src rev-parse HEAD 2>/dev/null || true)"
  dirty="$(git -C src status --porcelain --untracked-files=no 2>/dev/null || true)"
  if [[ $head == "$REV" && "$(cat "$STATE/synced" 2>/dev/null || true)" == "$REV" ]]; then
    # Koan's patches or branding may be applied; leave them be.
    echo "==> chromium/src is already at $VERSION; skipping gclient sync"
    sync=0
  elif [[ $head == "$REV" && -n $dirty ]]; then
    # A checkout from before the stamp existed, with Koan applied, so its
    # sync had finished.
    echo "==> chromium/src is already at $VERSION (Koan applied); skipping gclient sync"
    echo "$REV" > "$STATE/synced"
    sync=0
  elif [[ -n $dirty ]]; then
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
    if ((attempt == 3)); then
      echo "gclient sync failed 3 times. Run the same command again later:" \
        "it resumes (docs/BUILDING-MAC.md, Troubleshooting)." >&2
      exit 1
    fi
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
echo "$REV" > "$STATE/synced"

echo "==> Bootstrap complete: Chromium $VERSION"
