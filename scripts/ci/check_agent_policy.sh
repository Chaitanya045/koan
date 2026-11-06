#!/usr/bin/env bash
# Checks agent control's method policy against the DevTools protocol of a
# Chromium revision, without a Chromium checkout: only the protocol's PDL files
# are fetched from googlesource (Chromium's browser_protocol.pdl and the
# domains it includes, and V8's js_protocol.pdl at the revision DEPS pins).
#
#   scripts/ci/check_agent_policy.sh               # the pin (CHROMIUM_VERSION, line 2)
#   scripts/ci/check_agent_policy.sh 158.0.8127.2  # a release tag, or a commit, or a ref
#
# Runs scripts/agent/check_policy_coverage.py on them: a Chromium roll that adds
# a method, a parameter or an event to what an agent can reach fails here, not
# silently. Exits 0 if consistent, 1 if the policy needs attention, 2 if the
# revision doesn't exist, 3 if googlesource couldn't be reached.
#
# With KOAN_CHROMIUM_FILE_CACHE set, fetched files are kept there (files at a
# commit never change), as scripts/ci/check_patches.sh does.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
# shellcheck source=scripts/koan_chromium.sh
source "$ROOT/scripts/koan_chromium.sh"
koan_require_env
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

TARGET="${1:-$(koan_pin_commit)}"
rc=0
COMMIT="$(koan_resolve_commit "$TARGET")" || rc=$?
if [[ $rc == 1 ]]; then
  echo "no Chromium revision '$TARGET'" >&2
  exit 2
elif [[ $rc != 0 ]] || ! koan_is_commit "$COMMIT"; then
  echo "cannot reach chromium.googlesource.com to resolve '$TARGET'" >&2
  exit 3
fi
echo "Checking the agent policy against the protocol of Chromium $TARGET ($COMMIT)"

# fetch <repo url> <revision> <file> <destination under $WORK>
fetch() {
  local repo=$1 rev=$2 file=$3 dest=$4 cached http
  cached="${KOAN_CHROMIUM_FILE_CACHE:+$KOAN_CHROMIUM_FILE_CACHE/$rev/$file}"
  mkdir -p "$(dirname "$dest")"
  if [[ -n $cached && -f $cached ]]; then
    cp "$cached" "$dest"
    return
  fi
  # ?format=TEXT serves the file base64-encoded.
  http="$(curl -sSL --retry 6 --retry-delay 10 --retry-max-time 180 \
    -o "$dest.b64" -w '%{http_code}' "$repo/+/$rev/$file?format=TEXT")" ||
    { echo "cannot fetch $file" >&2; exit 3; }
  if [[ $http != 200 ]]; then
    echo "cannot fetch $file at $rev (HTTP $http)" >&2
    exit 3
  fi
  base64 -d <"$dest.b64" >"$dest"
  rm -f "$dest.b64"
  if [[ -n $cached ]]; then
    mkdir -p "$(dirname "$cached")"
    cp "$dest" "$cached.tmp.$$" && mv "$cached.tmp.$$" "$cached" || true
  fi
}

PROTOCOL_DIR=third_party/blink/public/devtools_protocol
fetch "$KOAN_GITILES" "$COMMIT" "$PROTOCOL_DIR/browser_protocol.pdl" \
  "$WORK/$PROTOCOL_DIR/browser_protocol.pdl"
while IFS= read -r included; do
  fetch "$KOAN_GITILES" "$COMMIT" "$PROTOCOL_DIR/$included" \
    "$WORK/$PROTOCOL_DIR/$included"
done < <(sed -n 's/^include[[:space:]]*\([^[:space:]]*\)[[:space:]]*$/\1/p' \
  "$WORK/$PROTOCOL_DIR/browser_protocol.pdl")

# V8 is a separate repository, at the revision DEPS pins.
fetch "$KOAN_GITILES" "$COMMIT" DEPS "$WORK/DEPS"
V8_REVISION="$(sed -n "s/^  'v8_revision': '\([0-9a-f]\{40\}\)',.*/\1/p" "$WORK/DEPS" | head -n 1)"
if ! koan_is_commit "$V8_REVISION"; then
  echo "cannot find v8_revision in DEPS at $COMMIT" >&2
  exit 3
fi
fetch "${KOAN_GITILES%/chromium/src}/v8/v8" "$V8_REVISION" include/js_protocol.pdl \
  "$WORK/v8/include/js_protocol.pdl"

exec python3 -I "$ROOT/scripts/agent/check_policy_coverage.py" --chromium-src "$WORK"
