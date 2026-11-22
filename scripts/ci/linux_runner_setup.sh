#!/usr/bin/env bash
# Linux CI: gets a GitHub-hosted ubuntu-24.04 runner ready for Koan's chain.
#
#   scripts/ci/linux_runner_setup.sh [--package]
#
# Frees disk space (the image comes with ~20 GB free; the source tree alone
# is ~20 GB) by deleting SDKs and toolchains the Chromium build never uses,
# and installs zstd. The fetch job also needs librsvg and ImageMagick for
# apply_branding.sh; Ubuntu's ImageMagick 6 has no `magick` command, so a
# small one that runs `convert` goes on PATH. --package also installs
# flatpak and flatpak-builder for scripts/ci/linux_package.sh.
#
# Exports KOAN_SCRATCH to $GITHUB_ENV: the disk with the most room for the
# archives (/mnt when the runner has that second disk, else RUNNER_TEMP).
set -euo pipefail

if [[ ${GITHUB_ACTIONS:-} != true || $(uname -s) != Linux ]]; then
  echo "linux_runner_setup.sh only runs on a GitHub Actions Linux runner" >&2
  exit 1
fi
PACKAGE=0
[[ ${1:-} != --package ]] || PACKAGE=1

echo "==> Machine"
. /etc/os-release && echo "$PRETTY_NAME"
echo "CPUs: $(nproc), memory: $(free -g | awk '/^Mem:/ { print $2 }') GB"
df -h / /mnt 2>/dev/null || df -h /

echo "==> Freeing disk space"
# Each of these is several GB on the image and unused by a Chromium build
# (it brings its own clang, Rust, node, Java and sysroot).
sudo rm -rf /usr/local/lib/android /usr/share/dotnet /opt/ghc /usr/local/.ghcup \
  /usr/share/swift /usr/local/share/powershell /usr/local/share/chromium \
  /usr/local/lib/node_modules /opt/microsoft /opt/google /opt/az \
  /usr/lib/jvm /usr/local/julia* /opt/hostedtoolcache/CodeQL \
  /opt/hostedtoolcache/go /opt/hostedtoolcache/PyPy /opt/hostedtoolcache/Ruby \
  /usr/local/share/boost /usr/local/aws-cli /usr/local/share/vcpkg || true
sudo docker image prune --all --force >/dev/null 2>&1 || true
sudo swapoff -a 2>/dev/null || true
sudo rm -f /mnt/swapfile /swapfile || true

echo "==> Packages"
# librsvg renders the logos for both: the branding and the Flatpak's icons.
pkgs=(zstd librsvg2-bin)
if [[ $PACKAGE == 1 ]]; then
  pkgs+=(flatpak flatpak-builder)
else
  pkgs+=(imagemagick)
fi
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "${pkgs[@]}" >/dev/null

if [[ $PACKAGE == 0 ]] && ! command -v magick >/dev/null; then
  bin="$RUNNER_TEMP/koan-bin"
  mkdir -p "$bin"
  printf '#!/bin/sh\nexec convert "$@"\n' > "$bin/magick"
  chmod +x "$bin/magick"
  echo "$bin" >> "$GITHUB_PATH"
fi

# 4 GB of swap: the link of the chrome binary is the peak; the runner's
# 16 GB fit it at 4 jobs, but leave no room for surprises.
scratch="$RUNNER_TEMP"
if mountpoint -q /mnt; then
  if sudo fallocate -l 4G /mnt/koan-swap && sudo chmod 600 /mnt/koan-swap &&
      sudo mkswap -q /mnt/koan-swap; then
    sudo swapon /mnt/koan-swap || true
  fi
  sudo mkdir -p /mnt/koan
  sudo chown "$(id -u):$(id -g)" /mnt/koan
  scratch=/mnt/koan
fi
echo "KOAN_SCRATCH=$scratch" >> "$GITHUB_ENV"
echo "archives go to $scratch"

df -h / /mnt 2>/dev/null || df -h /
