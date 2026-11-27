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
# flatpak and Flathub's org.flatpak.Builder for scripts/ci/linux_package.sh.
# apt reads only Ubuntu's archive (linux_apt_sources.sh), and the downloads
# are retried a few times.
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

# Runs a network fetch up to 4 times, 15 s apart, so a server's bad minute
# doesn't fail the job; a fetch that keeps failing still does.
#   retry <what> <command...>
retry() {
  local what="$1" n
  shift
  for n in 1 2 3 4; do
    "$@" && return 0
    [[ $n == 4 ]] || { echo "$what failed (try $n of 4), retrying in 15 s" >&2; sleep 15; }
  done
  echo "$what failed 4 times" >&2
  return 1
}

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
  pkgs+=(flatpak)
else
  pkgs+=(imagemagick)
fi
# Only Ubuntu's own archive, where all of these come from: the image's
# third-party sources (Microsoft's and others) fail the update whenever one
# of their servers does (linux_apt_sources.sh). A failure of Ubuntu's
# archive is still fatal, after the retries.
apt_sources="$RUNNER_TEMP/koan-apt-sources"
"$(dirname "$0")/linux_apt_sources.sh" /etc/apt "$apt_sources"
apt_opts=(-o "Dir::Etc::SourceList=/dev/null" -o "Dir::Etc::SourceParts=$apt_sources"
  -o Acquire::Retries=3)
retry "apt-get update" sudo apt-get update -qq "${apt_opts[@]}"
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
  "${apt_opts[@]}" "${pkgs[@]}" >/dev/null

# Flathub's flatpak-builder, the one linux_package.sh was checked with:
# Ubuntu's (1.4) fails appstreamcli compose on the metainfo
# ("E: file-read-error").
if [[ $PACKAGE == 1 ]]; then
  retry "Flathub's remote" flatpak remote-add --user --if-not-exists flathub \
    https://dl.flathub.org/repo/flathub.flatpakrepo
  retry org.flatpak.Builder \
    flatpak install --user -y --noninteractive flathub org.flatpak.Builder >/dev/null
fi

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
