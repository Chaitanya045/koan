#!/usr/bin/env bash
# Static proof that Koan's signing gives every rebuild the same designated
# requirement (#141), without touching the user's keychains, keychain items or
# the real signing identity.
#
#   scripts/mac_signing_check.sh selftest     sign two different dummy apps
#                                             with a throw-away identity and
#                                             compare (also the ad-hoc case)
#   scripts/mac_signing_check.sh X.app Y.app  compare two real signed builds:
#                                             same requirement, certificate-
#                                             based, and Y satisfies X's
#
# `selftest` makes a temporary keychain file under $TMPDIR (through
# mac_signing_identity.sh with KOAN_SIGNING_KEYCHAIN, so the keychain search
# list is restored), and removes the directory at the end. No item in any
# existing keychain is read or changed.
#
# What this proves: the ACL's trusted-application requirement stays valid
# across rebuilds. It does not prove the login keychain's partition list
# (needs a real item); see .agent/notes/mac.md.
# Bash 3.2 (macOS's) compatible.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=scripts/koan_platform.sh
source "$ROOT/scripts/koan_platform.sh"
if [[ $(koan_os) != mac ]]; then
  echo "macOS only" >&2
  exit 1
fi
koan_require_mac_env

# codesign prints an implied requirement (ad hoc: cdhash H"...") as a comment,
# "# designated => ...", and a signed one without the "# ".
dr() { codesign -d -r- "$1" 2>&1 | sed -n -e 's/^# designated => //p' -e 's/^designated => //p'; }

compare() {
  local x="$1" y="$2" drx dry rc=0
  drx="$(dr "$x")"
  dry="$(dr "$y")"
  echo "== codesign -d -r- $x"; echo "$drx"
  echo "== codesign -d -r- $y"; echo "$dry"
  echo "== team identifier"
  codesign -dvv "$x" 2>&1 | grep -E '^(Authority|TeamIdentifier|Signature)' || true
  if [[ -z $drx || $drx != "$dry" ]]; then
    echo "FAIL: the requirements differ"; rc=1
  else
    echo "ok: identical requirements"
  fi
  if [[ $drx != *'certificate leaf = H"'* || $drx == *cdhash* ]]; then
    echo "FAIL: the requirement is not certificate-based"; rc=1
  else
    echo "ok: certificate-based, not cdhash-based"
  fi
  # -R takes requirement text after a leading "=".
  if codesign --verify --deep --strict -R "=$drx" "$y"; then
    echo "ok: $y satisfies the requirement of $x"
  else
    echo "FAIL: $y does not satisfy the requirement of $x"; rc=1
  fi
  return $rc
}

# A tiny app bundle (dev.koan.Koan) whose executable differs with $2, like a rebuild.
mkapp() {
  mkdir -p "$1/Contents/MacOS"
  echo "int main(void) { return $2; }" |
    clang -x c - -o "$1/Contents/MacOS/Koan"
  cat >"$1/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>dev.koan.Koan</string>
<key>CFBundleExecutable</key><string>Koan</string>
<key>CFBundlePackageType</key><string>APPL</string>
</dict></plist>
PLIST
}

selftest() {
  local tmp rc=0
  tmp="$(mktemp -d "$TMPDIR/koan-signing-check.XXXXXX")"
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp'" EXIT
  export KOAN_SIGNING_KEYCHAIN="$tmp/check.keychain-db"
  "$ROOT/scripts/mac_signing_identity.sh" create
  mkapp "$tmp/A.app" 1
  mkapp "$tmp/B.app" 2
  mkapp "$tmp/C.app" 3
  "$ROOT/scripts/mac_sign.sh" "$tmp/A.app"
  "$ROOT/scripts/mac_sign.sh" "$tmp/B.app"
  compare "$tmp/A.app" "$tmp/B.app" || rc=1
  echo
  echo "== negative control: the same app signed ad hoc (what a build without the identity has)"
  codesign --force --deep --sign - "$tmp/C.app"
  [[ -n $(dr "$tmp/C.app") ]] || echo "FAIL: no requirement printed for the ad-hoc app, the control proves nothing"
  echo "ad hoc requirement: $(dr "$tmp/C.app")"
  mkapp "$tmp/D.app" 4
  codesign --force --deep --sign - "$tmp/D.app"
  echo "rebuild, ad hoc:    $(dr "$tmp/D.app")"
  if codesign --verify -R "=$(dr "$tmp/C.app")" "$tmp/D.app" 2>/dev/null; then
    echo "UNEXPECTED: the ad-hoc rebuild satisfies the first one's requirement"
  else
    echo "ok: an ad-hoc rebuild does not satisfy the earlier build's requirement (the cause of #141)"
  fi
  return $rc
}

case "${1:-}" in
  selftest) selftest ;;
  "") echo "usage: $0 selftest | X.app Y.app" >&2; exit 2 ;;
  *) compare "${1:?}" "${2:?usage: $0 X.app Y.app}" ;;
esac
