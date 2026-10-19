# Shared helpers for Koan test scripts. Source, don't run.
#
# Test profiles live on the big disk (not /tmp, which is a small tmpfs) and
# are always removed on exit. Component updates are disabled so fresh
# profiles don't download hundreds of MB of models.
KOAN_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KOAN_CHROME="$KOAN_ROOT/chromium/src/out/Koan/chrome"
KOAN_TEST_DIR="$KOAN_ROOT/.test-profiles"
mkdir -p "$KOAN_TEST_DIR"

# Flags for a full browser UI rendered off-screen (headless Ozone).
KOAN_HEADLESS_UI_FLAGS=(--ozone-platform=headless --in-process-gpu --disable-gpu
  --no-first-run --no-default-browser-check --disable-component-update
  --window-size=1280,800)

koan_new_profile() { mktemp -d "$KOAN_TEST_DIR/prof-XXXX"; }

# Removes test dirs; retries once because Chromium's child processes can
# still be writing into the profile for a moment after the browser exits.
koan_rm() { rm -rf "$@" 2>/dev/null || { sleep 1; rm -rf "$@"; }; }
