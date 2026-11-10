# shellcheck shell=bash
# Prerequisite checks for building Koan on a Mac (docs/BUILDING-MAC.md).
# Source, don't run. Works with macOS's bash 3.2.
#
# koan_check_prereqs probes the machine and prints one line per check:
#   ok    <what>
#   warn  <what>: <why, and what to do>
#   FAIL  <what>: <why, and what to do>
# and returns 1 if anything FAILed. Each koan_judge_* function only judges
# values it is given (so scripts/tests/test_build_prereqs.py can test them);
# the koan_probe_* functions read the machine. KOAN_FAKE_<NAME> overrides a
# probe, for the tests and for `scripts/build.sh --dry-run` on Linux.

# Thresholds. The tested machine: MacBook Air M4, 16 GB, macOS 27, Xcode 27.1.
KOAN_MIN_MACOS=15        # Xcode 26 needs macOS 15.6 or later
KOAN_TESTED_MACOS=27
KOAN_MIN_XCODE=26        # Xcode 26 split the Metal toolchain out (ANGLE)
KOAN_TESTED_XCODE=27
KOAN_DISK_FRESH_GB=100   # checkout (~31 GB) + out/Koan (~15 GB) + headroom
KOAN_DISK_CHECKOUT_GB=30 # checkout already there: out/Koan and a version bump
KOAN_MIN_RAM_GB=8
KOAN_GOOD_RAM_GB=16

# --- judges: koan_judge_<check> <values...>; print a line, return 1 on FAIL.

koan_judge_arch() { # <uname -m>
  if [[ $1 == arm64 ]]; then
    echo "ok    Apple Silicon (arm64)"
  else
    echo "FAIL  Apple Silicon: this Mac is $1. Koan builds for Apple Silicon" \
      "(M1 or later) only (build/koan-mac.gn, target_cpu arm64)."
    return 1
  fi
}

koan_judge_macos() { # <sw_vers -productVersion>
  local major=${1%%.*}
  if [[ ! $major =~ ^[0-9]+$ ]]; then
    echo "FAIL  macOS: cannot read the version ('$1')"
    return 1
  elif ((major < KOAN_MIN_MACOS)); then
    echo "FAIL  macOS $1: update to macOS $KOAN_MIN_MACOS or later" \
      "(System Settings > General > Software Update): Xcode $KOAN_MIN_XCODE" \
      "and later need macOS 15.6 or later."
    return 1
  elif ((major < KOAN_TESTED_MACOS)); then
    echo "warn  macOS $1: should work, but Koan is tested on macOS" \
      "$KOAN_TESTED_MACOS."
  else
    echo "ok    macOS $1"
  fi
}

koan_judge_xcode() { # <DEVELOPER_DIR> <xcodebuild -version first line, or "">
  local dir=$1 line=$2 version major
  if [[ -z $line ]]; then
    echo "FAIL  Xcode: no full Xcode at DEVELOPER_DIR=$dir. Install Xcode from" \
      "the App Store (the Command Line Tools are not enough), open it once," \
      "and if it is not /Applications/Xcode.app, export" \
      "DEVELOPER_DIR=<path to Xcode.app>/Contents/Developer."
    return 1
  fi
  version=${line#Xcode }
  major=${version%%.*}
  if [[ ! $major =~ ^[0-9]+$ ]]; then
    echo "FAIL  Xcode: cannot read the version ('$line')"
    return 1
  elif ((major < KOAN_MIN_XCODE)); then
    echo "FAIL  Xcode $version: Koan needs Xcode $KOAN_MIN_XCODE or later" \
      "(tested: $KOAN_TESTED_XCODE.1). Update it in the App Store."
    return 1
  elif ((major < KOAN_TESTED_XCODE)); then
    echo "warn  Xcode $version: should work, but Koan is tested with Xcode" \
      "$KOAN_TESTED_XCODE.1."
  else
    echo "ok    Xcode $version ($dir)"
  fi
}

koan_judge_xcode_license() { # <0 if accepted>
  if [[ $1 == 0 ]]; then
    echo "ok    Xcode licence accepted"
  else
    echo "FAIL  Xcode licence: not accepted yet. Run: sudo xcodebuild" \
      "-license accept (or open Xcode once and agree)."
    return 1
  fi
}

koan_judge_metal() { # <path to metal, or "">
  if [[ -n $1 ]]; then
    echo "ok    Metal toolchain ($1)"
  else
    echo "FAIL  Metal toolchain: not installed (Xcode 26 and later download it" \
      "separately; Chromium's ANGLE needs it). Run: xcodebuild" \
      "-downloadComponent MetalToolchain (about 700 MB, once)."
    return 1
  fi
}

koan_judge_disk() { # <free GB> <1 if chromium/src exists, else 0>
  local free=$1 need=$KOAN_DISK_FRESH_GB
  ((${2:-0} == 1)) && need=$KOAN_DISK_CHECKOUT_GB
  if ((free < need)); then
    echo "FAIL  Disk: $free GB free here, need about $need GB. Free up space," \
      "or clone Koan onto a disk with more room."
    return 1
  elif ((free < need + 20)); then
    echo "warn  Disk: $free GB free; about $need GB is needed, so it will be" \
      "tight."
  else
    echo "ok    Disk: $free GB free (about $need GB needed)"
  fi
}

koan_judge_ram() { # <GB of RAM>
  if (($1 < KOAN_MIN_RAM_GB)); then
    echo "FAIL  RAM: $1 GB. Building Chromium needs at least" \
      "$KOAN_MIN_RAM_GB GB ($KOAN_GOOD_RAM_GB GB recommended)."
    return 1
  elif (($1 < KOAN_GOOD_RAM_GB)); then
    echo "warn  RAM: $1 GB. It will build, slowly: $KOAN_GOOD_RAM_GB GB is" \
      "recommended. Close other apps while it builds."
  else
    echo "ok    RAM: $1 GB"
  fi
}

koan_judge_path() { # <clone path>
  if [[ $1 == *" "* ]]; then
    echo "FAIL  Path: '$1' has a space in it, which Chromium's build can't" \
      "handle. Clone Koan somewhere without spaces, e.g. ~/src/koan."
    return 1
  fi
  echo "ok    Path: $1"
}

koan_judge_tool() { # <name> <path, or ""> <how to get it>
  if [[ -n $2 ]]; then
    echo "ok    $1"
  else
    echo "FAIL  $1: not found. $3"
    return 1
  fi
}

# --- probes: print the value; KOAN_FAKE_<NAME> wins.

koan_probe_arch() { echo "${KOAN_FAKE_ARCH:-$(uname -m)}"; }
koan_probe_macos() { echo "${KOAN_FAKE_MACOS:-$(sw_vers -productVersion)}"; }
koan_probe_xcode() {
  if [[ -n ${KOAN_FAKE_XCODE+x} ]]; then echo "$KOAN_FAKE_XCODE"; return; fi
  [[ -x "$DEVELOPER_DIR/usr/bin/xcodebuild" ]] || return 0
  "$DEVELOPER_DIR/usr/bin/xcodebuild" -version 2>/dev/null | head -n 1
}
koan_probe_xcode_license() {
  if [[ -n ${KOAN_FAKE_XCODE_LICENSE+x} ]]; then echo "$KOAN_FAKE_XCODE_LICENSE"; return; fi
  # Exits non-zero until the licence is accepted; changes nothing.
  if xcodebuild -license check >/dev/null 2>&1; then echo 0; else echo 1; fi
}
koan_probe_metal() {
  if [[ -n ${KOAN_FAKE_METAL+x} ]]; then echo "$KOAN_FAKE_METAL"; return; fi
  # Not under CFFIXED_USER_HOME, where xcrun can't find the toolchain
  # (.agent/notes/mac.md).
  env -u CFFIXED_USER_HOME xcrun -f metal 2>/dev/null || true
}
koan_probe_disk_gb() { # <dir>
  if [[ -n ${KOAN_FAKE_DISK_GB:-} ]]; then echo "$KOAN_FAKE_DISK_GB"; return; fi
  df -Pk "$1" | awk 'NR == 2 { print int($4 / 1048576) }'
}
koan_probe_ram_gb() {
  if [[ -n ${KOAN_FAKE_RAM_GB:-} ]]; then echo "$KOAN_FAKE_RAM_GB"; return; fi
  if [[ $(uname -s) == Darwin ]]; then
    echo $(($(sysctl -n hw.memsize) / 1073741824))
  else
    awk '/MemTotal/ { print int($2 / 1048576 + 0.5) }' /proc/meminfo
  fi
}

# koan_check_prereqs <clone root>: every check; returns 1 if any FAILed.
koan_check_prereqs() {
  local root=$1 rc=0 have_src=0
  [[ -d $root/chromium/src/.git ]] && have_src=1
  : "${DEVELOPER_DIR:=/Applications/Xcode.app/Contents/Developer}"
  koan_judge_arch "$(koan_probe_arch)" || rc=1
  koan_judge_macos "$(koan_probe_macos)" || rc=1
  if koan_judge_xcode "$DEVELOPER_DIR" "$(koan_probe_xcode)"; then
    koan_judge_xcode_license "$(koan_probe_xcode_license)" || rc=1
    koan_judge_metal "$(koan_probe_metal)" || rc=1
  else
    rc=1
  fi
  koan_judge_disk "$(koan_probe_disk_gb "$root")" "$have_src" || rc=1
  koan_judge_ram "$(koan_probe_ram_gb)" || rc=1
  koan_judge_path "$root" || rc=1
  koan_judge_tool git "${KOAN_FAKE_GIT-$(command -v git)}" \
    "It comes with Xcode; check DEVELOPER_DIR." || rc=1
  koan_judge_tool python3 "${KOAN_FAKE_PYTHON3-$(command -v python3)}" \
    "It comes with Xcode; check DEVELOPER_DIR." || rc=1
  return "$rc"
}
