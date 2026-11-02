#!/usr/bin/env bash
# Prepares a GitHub-hosted macOS runner for a Chromium build:
#
#   scripts/ci/mac_runner_setup.sh
#
# - Frees disk space: other Xcodes, simulator runtimes, the Android SDK and
#   other toolchains Koan doesn't use (a Chromium checkout plus out/ needs
#   far more than the 14 GB GitHub guarantees).
# - Turns Spotlight indexing off, so it doesn't index the build tree.
# - Checks that the Xcode the build uses (KOAN_CI_XCODE, see mac_env.sh)
#   is there, and that its Metal toolchain works (installing it if not):
#   since Xcode 26 it is a separate download, and ANGLE compiles Metal
#   shaders.
# - Prints the machine: CPUs, memory, disk.
#
# Only for CI runners: it deletes system-wide software.
set -euo pipefail

if [[ ${GITHUB_ACTIONS:-} != true || $(uname -s) != Darwin ]]; then
  echo "mac_runner_setup.sh only runs on a GitHub Actions macOS runner" >&2
  exit 1
fi

XCODE="${KOAN_CI_XCODE:-/Applications/Xcode_27.1.app}"
if [[ ! -x "$XCODE/Contents/Developer/usr/bin/xcodebuild" ]]; then
  echo "no Xcode at $XCODE; this runner has:" >&2
  ls -d /Applications/Xcode*.app >&2 || true
  exit 1
fi

echo "==> Machine"
sw_vers
echo "CPUs: $(sysctl -n hw.ncpu) ($(sysctl -n machdep.cpu.brand_string))"
echo "Memory: $(($(sysctl -n hw.memsize) / 1073741824)) GB"
df -h /

echo "==> Spotlight off"
sudo mdutil -a -i off >/dev/null || true

echo "==> Freeing disk space"
# Xcode is resolved through its real path: /Applications/Xcode.app and the
# version aliases are symlinks.
keep="$(cd "$XCODE" && pwd -P)"
for x in /Applications/Xcode*.app; do
  [[ -L $x ]] && continue
  if [[ "$(cd "$x" && pwd -P)" != "$keep" ]]; then
    echo "removing $x"
    sudo rm -rf "$x"
  fi
done
# Dangling version aliases of the removed Xcodes.
for x in /Applications/Xcode*.app; do
  if [[ -L $x && ! -e $x ]]; then sudo rm -f "$x"; fi
done
sudo xcode-select --switch "$XCODE/Contents/Developer"
export DEVELOPER_DIR="$XCODE/Contents/Developer"
# Compile a shader once now: the first `xcrun metal` mounts the toolchain
# (a cryptex), and when that first call came from inside the build, under
# load, it failed with "missing Metal Toolchain" after 96 s.
metal_check() {
  local d
  d="$(mktemp -d)"
  printf 'kernel void k(device float *a [[buffer(0)]]) { a[0] = 1; }\n' > "$d/k.metal"
  xcrun metal -c "$d/k.metal" -o "$d/k.air"
  local status=$?
  rm -rf "$d"
  return "$status"
}
if ! metal_check; then
  echo "==> Installing the Metal toolchain"
  xcodebuild -downloadComponent MetalToolchain
  metal_check
fi
xcodebuild -showComponent MetalToolchain
xcrun simctl delete all >/dev/null 2>&1 || true
xcrun simctl runtime delete all >/dev/null 2>&1 || true
for d in \
  "${ANDROID_HOME:-$HOME/Library/Android/sdk}" \
  "$HOME/.dotnet" \
  "$HOME/.ghcup" \
  "$HOME/.rustup" \
  "$HOME/.cargo" \
  "$HOME/hostedtoolcache" \
  "${AGENT_TOOLSDIRECTORY:-/nonexistent}" \
  /Library/Developer/CoreSimulator/Profiles/Runtimes \
  /usr/local/share/powershell \
  /usr/local/share/edge_driver \
  /usr/local/share/chromedriver-mac-arm64 \
  /usr/local/lib/node_modules \
  "/Applications/Google Chrome.app" \
  "/Applications/Firefox.app" \
  "/Applications/Microsoft Edge.app"; do
  if [[ -e $d ]]; then
    echo "removing $d"
    sudo rm -rf "$d"
  fi
done
df -h /
