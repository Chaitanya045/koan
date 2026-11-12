#!/usr/bin/env bash
# VMP-signs Koan.app for Widevine (issue #315, docs/USAGE.md "DRM video").
#
#   scripts/mac/widevine_vmp_sign.sh [--if-configured] [--verify] <Koan.app>
#
# Widevine's Verified Media Path (VMP) on macOS: the CDM hashes the browser's
# framework binary and checks it against a signature file next to it,
# <Name> Framework.framework/Versions/<v>/Resources/<Name> Framework.sig
# (chrome/common/media/cdm_host_file_path.cc). Without a valid signature the
# CDM still plays, but services that require VMP (Netflix) refuse the
# licence. The build writes an empty placeholder there
# (src/build/widevine_signature_placeholder.py); this replaces it with a
# signature from castLabs' EVS service (pip package castlabs-evs).
#
# Order: run it after the app is assembled and BEFORE Apple code signing.
# The .sig sits in the framework's Resources, which the code signature
# seals; the VMP hash covers the binary from its first __TEXT section to the
# end of its string table, so the code signature added afterwards doesn't
# change it (castLabs: "On macOS the VMP-signing needs to take place BEFORE
# the code-signing"). scripts/ci/mac_package.sh calls it before codesign.
#
# Credentials, in this order:
#   - EVS_ACCOUNT_NAME + EVS_PASSWD in the environment (CI: GitHub secrets
#     of the same names); runs non-interactively;
#   - else the login cached by `python3 -m castlabs_evs.account signup` or
#     `... account reauth` in ~/.config/evs/config.json of the real home
#     (the Mac; EVS_CONFIG_FILE overrides).
# With neither, nothing is signed: --if-configured exits 0 quietly (build.sh,
# CI without the secrets), otherwise it exits 1.
#
# A refused or failed signature leaves the placeholder and exits 0 with a
# warning (DRM still works where VMP isn't required), unless
# KOAN_VMP_REQUIRED=1, which makes it fail. --verify only checks the
# existing signature (exit 1 if it isn't valid).
#
# castlabs-evs goes into a virtualenv in $KOAN_MAC/evs-venv (or
# .cache/mac/evs-venv), pinned to EVS_VERSION.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
EVS_VERSION="${KOAN_EVS_VERSION:-1.3.3}"
IF_CONFIGURED=0
VERIFY_ONLY=0
while (($#)); do
  case $1 in
    --if-configured) IF_CONFIGURED=1 ;;
    --verify) VERIFY_ONLY=1 ;;
    -h | --help) sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p;}' "$0"; exit 0 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) break ;;
  esac
  shift
done
app="${1:?usage: $0 [--if-configured] [--verify] <Koan.app>}"
[[ -d $app ]] || { echo "no $app" >&2; exit 1; }

name="$(basename "$app" .app)"
fw="$app/Contents/Frameworks/$name Framework.framework/Versions/Current"
bin="$fw/$name Framework"
sig="$fw/Resources/$name Framework.sig"
[[ -f $bin ]] || { echo "no framework binary $bin" >&2; exit 1; }

warn() {
  if [[ -n ${GITHUB_ACTIONS:-} ]]; then
    echo "::warning title=Widevine VMP::$*"
  else
    echo "warning: $*" >&2
  fi
}

# The login a plain Terminal's `castlabs_evs.account signup` cached, even
# when Koan's env.sh points HOME into $KOAN_MAC. CI's fake HOME is fine: it
# logs in from the secrets.
if [[ -z ${EVS_CONFIG_FILE:-} ]]; then
  if [[ -n ${CI:-} ]]; then
    EVS_CONFIG_FILE="$HOME/.config/evs/config.json"
  else
    EVS_CONFIG_FILE="$(koan_real_home)/.config/evs/config.json"
  fi
fi
export EVS_CONFIG_FILE
evs_config="$EVS_CONFIG_FILE"
if [[ $VERIFY_ONLY == 0 && -z ${EVS_ACCOUNT_NAME:-} && ! -f $evs_config ]]; then
  if [[ $IF_CONFIGURED == 1 ]]; then
    echo "Widevine VMP: no EVS login (EVS_ACCOUNT_NAME or $evs_config); $name stays unsigned for VMP"
    exit 0
  fi
  echo "no EVS login: set EVS_ACCOUNT_NAME and EVS_PASSWD, or run" \
    "python3 -m castlabs_evs.account signup (docs/USAGE.md, \"DRM video\")" >&2
  exit 1
fi

venv="${KOAN_MAC:-$ROOT/.cache/mac}/evs-venv"
if ! "$venv/bin/python3" -I -c "import castlabs_evs" 2>/dev/null ||
    [[ "$("$venv/bin/python3" -I -c 'import castlabs_evs; print(castlabs_evs.__version__)')" != "$EVS_VERSION" ]]; then
  echo "==> Installing castlabs-evs $EVS_VERSION into $venv"
  python3 -m venv "$venv"
  "$venv/bin/python3" -m pip install --quiet --disable-pip-version-check \
    "castlabs-evs==$EVS_VERSION"
fi
evs=("$venv/bin/python3" -I -m castlabs_evs.vmp)
# No prompts without a terminal (CI) or when the password is given.
if [[ ! -t 0 || -n ${EVS_PASSWD:-} ]]; then evs+=(--no-ask); fi

if [[ $VERIFY_ONLY == 1 ]]; then
  exec "${evs[@]}" verify "$bin" "$sig"
fi

echo "==> Widevine VMP signature for $name Framework"
# `sign` keeps a signature that is still valid for the binary (and has 90+
# days left) and asks EVS for a new one otherwise.
if "${evs[@]}" sign "$bin" "$sig" && "${evs[@]}" verify "$bin" "$sig"; then
  echo "VMP signature: $sig"
  exit 0
fi
msg="EVS did not sign $name Framework, so it stays without VMP (Netflix refuses the licence; non-VMP DRM still plays). See docs/USAGE.md, \"DRM video\"."
if [[ ${KOAN_VMP_REQUIRED:-0} == 1 ]]; then
  echo "$msg" >&2
  exit 1
fi
warn "$msg"
