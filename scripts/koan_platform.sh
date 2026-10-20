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
