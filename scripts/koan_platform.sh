# shellcheck shell=bash
# Platform detection shared by Koan's scripts. Source, don't run.
#
# Works with macOS's bash 3.2: no associative arrays, mapfile or ${var,,}.

# The host platform: "linux" or "mac". Exits on anything else.
koan_os() {
  case "$(uname -s)" in
    Linux) echo linux ;;
    Darwin) echo mac ;;
    *) echo "unsupported platform: $(uname -s)" >&2; exit 1 ;;
  esac
}

# macOS: everything Koan's tools write stays in one folder (KOAN_MAC, e.g.
# ~/work/koan-mac). Its env.sh points HOME, TMPDIR and the tool caches into
# it; refuse to run without it, so caches (vpython, CIPD, siso, ...) never
# land in the real home directory. See .agent/notes/mac.md.
koan_require_mac_env() {
  local hint="source the folder's env.sh first, e.g. source ~/work/koan-mac/env.sh"
  if [[ -z ${KOAN_MAC:-} || ! -d $KOAN_MAC ]]; then
    echo "KOAN_MAC is not set: $hint" >&2
    exit 1
  fi
  if [[ $HOME != "$KOAN_MAC"/* || ${TMPDIR:-} != "$KOAN_MAC"/* ]]; then
    echo "HOME and TMPDIR must be inside KOAN_MAC=$KOAN_MAC: $hint" >&2
    exit 1
  fi
  mkdir -p "$HOME" "$TMPDIR"
  # Apple's tools (xcodebuild, actool, ibtoold) find the home directory
  # through CoreFoundation, which ignores HOME but honours this.
  export CFFIXED_USER_HOME="$HOME"
}

# macOS: Chromium needs a full Xcode, chosen with DEVELOPER_DIR rather than
# xcode-select (whose system-wide choice is often the Command Line Tools).
# Defaults DEVELOPER_DIR to /Applications/Xcode.app and exports it.
koan_require_xcode() {
  : "${DEVELOPER_DIR:=/Applications/Xcode.app/Contents/Developer}"
  if [[ ! -x "$DEVELOPER_DIR/usr/bin/xcodebuild" ]]; then
    echo "no full Xcode at DEVELOPER_DIR=$DEVELOPER_DIR" >&2
    echo "install Xcode and set DEVELOPER_DIR to" \
      "<path to Xcode.app>/Contents/Developer" >&2
    exit 1
  fi
  export DEVELOPER_DIR
}

# The checks every script that runs on macOS starts with. No-op on Linux.
koan_require_env() {
  if [[ $(koan_os) == mac ]]; then
    koan_require_mac_env
    koan_require_xcode
  fi
}

# Memory the system can give a build right now, in whole GB.
koan_available_gb() {
  if [[ $(koan_os) == mac ]]; then
    # Free, inactive, speculative and purgeable pages can all be reused.
    vm_stat | awk '
      /page size of/ { for (i = 1; i <= NF; i++) if ($i == "of") size = $(i + 1) }
      /^Pages (free|inactive|speculative|purgeable):/ { gsub(/\./, "", $NF); pages += $NF }
      END { print int(pages * size / 1073741824) }'
  else
    awk '/MemAvailable/ { print int($2 / 1048576) }' /proc/meminfo
  fi
}

# Parallel compile jobs: conservative (each can take about 1 GB), more when
# plenty of memory is free. Same thresholds as the shared agent build.
koan_default_jobs() {
  local gb
  gb="$(koan_available_gb)"
  if ((gb >= 9)); then echo 6; elif ((gb >= 6)); then echo 4; else echo 2; fi
}
