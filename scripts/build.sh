#!/usr/bin/env bash
# Configure and build Koan.
#
# macOS (Apple Silicon), the one command (docs/BUILDING-MAC.md):
#   scripts/build.sh [options] [target]
#
#   1. checks the prerequisites (Apple Silicon, macOS, Xcode and its licence,
#      the Metal toolchain, disk, RAM) and says how to fix what is missing;
#   2. fetches depot_tools and Chromium at CHROMIUM_VERSION
#      (scripts/bootstrap.sh), or, after a `git pull` that moved the pin,
#      moves the checkout there (scripts/update_chromium.sh);
#   3. applies Koan's patches and branding when they changed;
#   4. writes out/Koan/args.gn (build/koan-mac.gn: release, no symbols) and
#      runs gn gen;
#   5. builds at a laptop-friendly -j, under nice and caffeinate, then signs
#      Koan.app with the local identity if you created one
#      (scripts/mac_signing_identity.sh create; KOAN_SIGN=0 skips that);
#   6. copies Koan.app to ~/Applications (by default when KOAN_MAC is unset,
#      i.e. on a friend's clone; --install / --no-install to choose).
#
#   Every step is skipped when already done, so after an interruption (a
#   closed lid, Ctrl-C, a network error) run it again and it resumes. To
#   update: git pull && scripts/build.sh.
#   KOAN_MAC (the maintainer's one folder, .agent/notes/mac.md) is optional:
#   without it the tool caches go into .cache/mac in the clone.
#
# Linux:
#   scripts/build.sh [target]                       development build (out/Koan)
#   KOAN_CONFIG=release scripts/build.sh [target]   release build (out/Release)
#   Only builds: run bootstrap, apply_patches and apply_branding first.
#
# Options:
#   --fast        all CPU cores, no nice: quicker, but the Mac is busy
#   --install     copy Koan.app to ~/Applications after the build
#   --no-install  don't
#   --no-sync     macOS: skip steps 1-3 (fetch, patches, branding)
#   --check       macOS: only check the prerequisites
#   --dry-run     print what each step would do, change nothing, fetch
#                 nothing (runs on Linux too; CI uses it)
#   --gen         only gn gen (CI uses it to check the build files before
#                 the long build); no other step
#
# JOBS (parallel compile jobs) defaults to 2, 4 or 6 depending on the memory
# available right now, so a build fits next to other apps in 16 GB of RAM.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
# shellcheck source=scripts/koan_chromium.sh
source "$ROOT/scripts/koan_chromium.sh"
# shellcheck source=scripts/koan_prereqs.sh
source "$ROOT/scripts/koan_prereqs.sh"

usage() { sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p;}' "$0"; }

TARGET=chrome
FAST=0
INSTALL=auto
# CI (macos.yml) fetches and applies in its own job, then deletes Chromium's
# .git; its build slices must only build.
SYNC=1
[[ -z ${CI:-} ]] || SYNC=0
MODE=build
while (($#)); do
  case $1 in
    --fast) FAST=1 ;;
    --install) INSTALL=1 ;;
    --no-install) INSTALL=0 ;;
    --no-sync) SYNC=0 ;;
    --check) MODE=check ;;
    --dry-run) MODE=dry ;;
    --gen) MODE=gen ;;
    -h | --help) usage; exit 0 ;;
    -*) echo "unknown option $1 (scripts/build.sh --help)" >&2; exit 2 ;;
    *) TARGET=$1 ;;
  esac
  shift
done

OS="$(koan_os)"
# --dry-run may plan for a Mac on Linux (CI): nothing runs.
if [[ $MODE == dry && -n ${KOAN_FAKE_OS:-} ]]; then OS=$KOAN_FAKE_OS; fi
if [[ $MODE != dry ]]; then koan_require_env; fi
export PATH="$ROOT/depot_tools:$PATH"
SRC="$ROOT/chromium/src"
STATE="$ROOT/.cache/build-state"
VERSION="$(koan_pin_version)"
REV="$(koan_pin_commit)"

# Install by default on a friend's clone (no KOAN_MAC of their own), not on
# the maintainer's Mac (their env.sh) or in CI.
if [[ $INSTALL == auto ]]; then
  INSTALL=0
  if [[ $OS == mac && -z ${CI:-} &&
        ( -z ${KOAN_MAC:-} || ${KOAN_MAC:-} == "$ROOT/.cache/mac" ) ]]; then
    INSTALL=1
  fi
fi

if [[ $OS == mac ]]; then
  case "${KOAN_CONFIG:-mac}" in
    mac) OUT=out/Koan; ARGS="$ROOT/build/koan-mac.gn" ;;
    *) echo "on macOS, leave KOAN_CONFIG unset (build/koan-mac.gn)" >&2
       exit 2 ;;
  esac
else
  case "${KOAN_CONFIG:-dev}" in
    dev) OUT=out/Koan; ARGS="$ROOT/build/koan.gn" ;;
    release) OUT=out/Release; ARGS="$ROOT/build/koan-release.gn" ;;
    *) echo "KOAN_CONFIG must be dev or release" >&2; exit 2 ;;
  esac
fi

cpus() {
  if [[ $(uname -s) == Darwin ]]; then sysctl -n hw.ncpu; else nproc; fi
}
if [[ $FAST == 1 ]]; then
  JOBS="${JOBS:-$(cpus)}"
else
  JOBS="${JOBS:-$(koan_default_jobs)}"
fi

step() { echo "==> $*"; }

# A fingerprint of everything steps 3 apply: the patches, the branding
# sources and the scripts that apply them. Changes after a git pull.
koan_inputs_hash() {
  (cd "$ROOT" && find patches branding scripts/branding.py \
      scripts/apply_patches.sh scripts/apply_branding.sh -type f \
      ! -path 'branding/out/*' | LC_ALL=C sort | while IFS= read -r f; do
        echo "$f"; cat "$f"; done) | git hash-object --stdin
}

# Where chromium/src stands: missing | partial | bump | ready.
chromium_state() {
  if [[ ! -d $SRC/.git ]]; then
    echo missing
  elif [[ "$(git -C "$SRC" rev-parse HEAD 2>/dev/null || true)" != "$REV" ]]; then
    # A finished sync of another version: CHROMIUM_VERSION moved (git pull).
    if [[ -f $STATE/synced ]]; then echo bump; else echo partial; fi
  elif [[ "$(cat "$STATE/synced" 2>/dev/null || true)" == "$REV" ]]; then
    echo ready
  else
    echo partial
  fi
}

koan_state() {
  if [[ "$(cat "$STATE/applied" 2>/dev/null || true)" == "$(koan_inputs_hash)" ]]; then
    echo applied
  else
    echo pending
  fi
}

# Step 3. Brings chromium/src's Koan changes to this clone's patches and
# branding, touching only files whose patch changed (each touched file
# rebuilds what includes it). An unchanged patch stays applied; a changed or
# removed one is put back to pristine first. Changes that are no patch's
# stop the build: they are someone's own work.
sync_koan() {
  local patched_files="$ROOT/.cache/patched-files" owned modified branded f
  if [[ "$(koan_state)" == applied && -L $SRC/koan ]]; then
    step "Koan's patches and branding are applied"
    return
  fi
  step "Applying Koan's patches and branding"
  owned="$({ (cd "$ROOT/patches" && find . -name '*.patch' | sed -E 's#^\./##; s#\.patch$##')
             [[ ! -f $patched_files ]] || cat "$patched_files"; } | sort -u)"
  branded="$("$ROOT/scripts/apply_branding.sh" --list)"
  modified="$(cd "$SRC" && git -c core.quotePath=false diff --name-only HEAD |
    grep -vxF -f <(echo "$branded") || true)"
  while IFS= read -r f; do
    [[ -n $f ]] || continue
    if ! grep -qxF -- "$f" <<<"$owned"; then
      echo "chromium/src/$f has changes that are not Koan's patches." >&2
      echo "Keep them elsewhere, then: git -C chromium/src checkout -- $f" >&2
      exit 1
    fi
    if [[ -f $ROOT/patches/$f.patch ]] &&
        (cd "$SRC" && git apply --reverse --check "$ROOT/patches/$f.patch" 2>/dev/null); then
      continue
    fi
    (cd "$SRC" && git checkout -q -- "$f")
  done <<<"$modified"
  if ! "$ROOT/scripts/apply_patches.sh" apply; then
    echo "Some patches don't apply to Chromium $VERSION. If you changed" \
      "nothing yourself, the branch you pulled is broken: tell the" \
      "maintainer (docs/BUILDING-MAC.md, Troubleshooting)." >&2
    exit 1
  fi
  { [[ ! -f $patched_files ]] || cat "$patched_files"
    (cd "$ROOT/patches" && find . -name '*.patch' | sed -E 's#^\./##; s#\.patch$##')
  } | sort -u > "$patched_files.new"
  mv "$patched_files.new" "$patched_files"
  "$ROOT/scripts/apply_branding.sh"
  mkdir -p "$STATE"
  koan_inputs_hash > "$STATE/applied"
}

fetch_chromium() {
  local state
  state="$(chromium_state)"
  case $state in
    ready) step "Chromium $VERSION is checked out" ;;
    bump)
      step "CHROMIUM_VERSION moved to $VERSION: updating the checkout"
      "$ROOT/scripts/update_chromium.sh" "$VERSION"
      echo "$REV" > "$STATE/synced"
      # update_chromium.sh re-applied everything from pristine.
      koan_inputs_hash > "$STATE/applied"
      ;;
    *)
      step "Fetching Chromium $VERSION (about 30 GB; resumes if interrupted)"
      "$ROOT/scripts/bootstrap.sh"
      ;;
  esac
}

write_args() {
  cd "$SRC"
  mkdir -p "$OUT"
  # Only rewrite args.gn when it changes: gn gen then costs nothing.
  local new="$OUT/args.gn.new"
  cp "$ARGS" "$new"
  if [[ $OS == mac ]]; then
    # ANGLE compiles Metal shaders. Since Xcode 26 the Metal toolchain is a
    # separate component (xcodebuild -downloadComponent MetalToolchain), and
    # xcrun finds it through `xcodebuild -find`, which fails ("'metallib' not
    # found") under the CFFIXED_USER_HOME that koan_require_mac_env sets. So
    # ANGLE runs the toolchain directly, through a link in the out dir that
    # stays put when the toolchain's mount point changes (on every boot).
    local metal
    metal="$(env -u CFFIXED_USER_HOME xcrun -f metal 2>/dev/null || true)"
    if [[ ! -x $metal ]]; then
      echo "no Metal toolchain: run xcodebuild -downloadComponent MetalToolchain" >&2
      exit 1
    fi
    ln -sfn "$(dirname "$metal")" "$OUT/metal-toolchain"
    echo "angle_metal_toolchain_dir = \"$PWD/$OUT/metal-toolchain\"" >> "$new"
  fi
  if cmp -s "$new" "$OUT/args.gn"; then rm "$new"; else mv "$new" "$OUT/args.gn"; fi
}

app_name() {
  # The bundle is named after PRODUCT_FULLNAME in the BRANDING file (Koan
  # once scripts/apply_branding.sh has run, Chromium before).
  sed -n 's/^PRODUCT_FULLNAME=//p' "$SRC/chrome/app/theme/chromium/BRANDING"
}

install_app() {
  local name dest app
  name="$(app_name)"
  app="$SRC/$OUT/$name.app"
  dest="$(koan_real_home)/Applications"
  if pgrep -xq "$name"; then
    echo "$name is running: quit it (⌘Q), then run scripts/build.sh again" \
      "to install (nothing is rebuilt)." >&2
    exit 1
  fi
  step "Installing $dest/$name.app"
  mkdir -p "$dest"
  rm -rf "$dest/$name.app.new"
  ditto "$app" "$dest/$name.app.new"
  rm -rf "$dest/$name.app"
  mv "$dest/$name.app.new" "$dest/$name.app"
}

dry_run() {
  local yes=would no=skip
  echo "Koan build plan (dry run: nothing changes, nothing is fetched)"
  echo "  platform     $OS"
  echo "  Chromium     $VERSION ($REV)"
  echo "  args         $ARGS -> chromium/src/$OUT/args.gn"
  echo "  jobs         $JOBS$([[ $FAST == 1 ]] && echo ' (--fast)' || echo ' (nice -n 10)')"
  if [[ $OS == mac ]]; then
    echo "Prerequisites:"
    koan_check_prereqs "$ROOT" | sed 's/^/  /' || true
    if [[ $SYNC == 1 ]]; then
      case "$(chromium_state)" in
        ready) echo "  fetch        $no: Chromium $VERSION is checked out" ;;
        bump) echo "  fetch        $yes: move the checkout to $VERSION (update_chromium.sh)" ;;
        partial) echo "  fetch        $yes: resume the fetch (bootstrap.sh)" ;;
        missing) echo "  fetch        $yes: fetch depot_tools and Chromium (bootstrap.sh)" ;;
      esac
      if [[ -d $SRC/.git && "$(koan_state)" == applied ]]; then
        echo "  koan         $no: patches and branding are applied"
      else
        echo "  koan         $yes: apply patches and branding"
      fi
    fi
  fi
  echo "  gn gen       $yes"
  echo "  build        $yes: autoninja -C $OUT -j $JOBS $TARGET"
  if [[ $OS == mac && $TARGET == chrome && ${KOAN_SIGN:-1} != 0 ]]; then
    echo "  sign         $yes: scripts/mac_sign.sh (the local identity, else ad hoc)"
  else
    echo "  sign         $no"
  fi
  if [[ $INSTALL == 1 && $TARGET == chrome ]]; then
    echo "  install      $yes: copy Koan.app to ~/Applications"
  else
    echo "  install      $no"
  fi
}

case $MODE in
  dry) dry_run; exit 0 ;;
  check)
    [[ $OS == mac ]] || { echo "--check is for macOS" >&2; exit 2; }
    koan_check_prereqs "$ROOT"
    exit
    ;;
esac

if [[ $OS == mac ]]; then
  echo "Xcode: $DEVELOPER_DIR"
  if [[ $MODE == build && $SYNC == 1 ]]; then
    mkdir -p "$ROOT/logs" "$STATE"
    log="$ROOT/logs/build-$(date +%Y%m%d-%H%M%S).log"
    exec > >(tee "$log") 2>&1
    echo "Log: $log"
    if [[ -z ${CI:-} ]]; then
      step "Checking prerequisites"
      if ! koan_check_prereqs "$ROOT"; then
        echo "Fix the FAIL lines above, then run scripts/build.sh again" \
          "(docs/BUILDING-MAC.md)." >&2
        exit 1
      fi
    fi
    fetch_chromium
    sync_koan
  fi
fi

echo "$JOBS compile jobs (set JOBS to change$([[ $FAST == 1 ]] || echo ', --fast for all cores'))"
write_args
gn gen "$OUT"
if [[ $MODE == gen ]]; then
  echo "generated $PWD/$OUT"
  exit 0
fi

build=(autoninja -C "$OUT" -j "$JOBS" "$TARGET")
if [[ $FAST == 0 ]]; then build=(nice -n 10 "${build[@]}"); fi
# Keep the Mac awake (not the display) while it builds.
if [[ $OS == mac ]]; then build=(caffeinate -i "${build[@]}"); fi
"${build[@]}"

if [[ $TARGET == chrome ]]; then
  if [[ $OS == mac ]]; then
    echo "built $PWD/$OUT/$(app_name).app"
    # Same signature on every build, so the saved Keychain item (the key for
    # cookies and passwords) keeps matching; see scripts/mac_sign.sh. Before
    # the install, so the installed copy is the signed one.
    if [[ ${KOAN_SIGN:-1} != 0 ]]; then
      "$ROOT/scripts/mac_sign.sh" "$PWD/$OUT/$(app_name).app"
    fi
    if [[ $INSTALL == 1 ]]; then
      install_app
      echo "Open Koan from ~/Applications (Finder, Spotlight or Launchpad)."
    else
      echo "test: scripts/run_mac.sh (profile inside \$KOAN_MAC)"
    fi
  else
    echo "built $PWD/$OUT/chrome"
  fi
fi
