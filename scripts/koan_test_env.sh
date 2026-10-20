# shellcheck shell=bash
# Shared helpers for Koan test scripts. Source, don't run.
# The variables are used by the scripts that source this file.
# shellcheck disable=SC2034
#
# Test profiles live on the big disk (not /tmp, which is a small tmpfs) and
# are always removed on exit. Component updates are disabled so fresh
# profiles don't download hundreds of MB of models.
if [[ $(uname -s) != Linux ]]; then
  # Headless Ozone (off-screen UI) exists only on Linux.
  echo "Koan's off-screen test tools are Linux-only" >&2
  exit 1
fi
KOAN_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KOAN_CHROME="$KOAN_ROOT/chromium/src/out/Koan/chrome"
KOAN_TEST_DIR="$KOAN_ROOT/.test-profiles"
mkdir -p "$KOAN_TEST_DIR"

# Flags for a full browser UI rendered off-screen (headless Ozone).
# --password-store=basic: throwaway profiles needn't use the desktop
# keyring; while it is locked (e.g. after a reboot with nobody logged in),
# Chromium waits for it to encrypt cookies and no page loads.
# --ozone-override-screen-size: the headless screen defaults to 1x1, which
# clamps menus (context menus) to nothing; make it as big as the window.
# shellcheck disable=SC2054  # "1280,800" is one value, not two elements
KOAN_HEADLESS_UI_FLAGS=(--ozone-platform=headless --in-process-gpu --disable-gpu
  --no-first-run --no-default-browser-check --disable-component-update
  --password-store=basic --window-size=1280,800
  --ozone-override-screen-size=1280,800)

koan_new_profile() { mktemp -d "$KOAN_TEST_DIR/prof-XXXX"; }

# Removes test dirs; retries once because Chromium's child processes can
# still be writing into the profile for a moment after the browser exits.
koan_rm() { rm -rf "$@" 2>/dev/null || { sleep 1; rm -rf "$@"; }; }
