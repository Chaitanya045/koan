#!/usr/bin/env bash
# Koan's local code-signing identity on macOS.
#
#   scripts/mac_signing_identity.sh status   print the identity's SHA-1 hash;
#                                            exit 1 (printing nothing) if it
#                                            doesn't exist yet
#   scripts/mac_signing_identity.sh create   create it once; does nothing if it
#                                            exists
#
# Why: a Keychain item's access control names the app by its code signature.
# A build without a certificate is signed ad hoc, and its signature (the
# CDHash) changes with every build, so "Koan Safe Storage" (the key that
# encrypts cookies and passwords) stops matching after a rebuild: macOS asks
# again, or, if that is dismissed, Koan can't read the key ("Relaunch ... to
# keep it encrypted", #141). Signing every build with one certificate keeps
# the designated requirement
#   identifier "dev.koan.Koan" and certificate leaf = H"<this hash>"
# the same across rebuilds, so the saved item keeps matching.
#
# The identity ("Koan Local Signing") is a self-signed code-signing
# certificate in a keychain of its own, $KOAN_MAC/keychains/koan-signing.keychain-db.
# It is not in the login keychain, not in the keychain search list, and not
# trusted system-wide: it only signs builds on this Mac. Nothing here ever
# deletes or changes an existing keychain item or keychain; `create` makes
# one new keychain file.
#
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

NAME="Koan Local Signing"
KEYCHAIN_DIR="$KOAN_MAC/keychains"
KEYCHAIN="$KEYCHAIN_DIR/koan-signing.keychain-db"
# The keychain holds one key that only signs local builds; its password
# protects nothing worth guarding, and codesign needs the keychain unlocked.
KEYCHAIN_PASSWORD="koan-local-signing"

# The certificate's SHA-1 hash, or nothing.
identity_hash() {
  [[ -f $KEYCHAIN ]] || return 0
  security find-certificate -c "$NAME" -Z "$KEYCHAIN" 2>/dev/null |
    sed -n 's/^SHA-1 hash: //p' | head -1
}

create() {
  local hash work
  hash="$(identity_hash)"
  if [[ -n $hash ]]; then
    echo "$NAME already exists ($hash)" >&2
    return 0
  fi
  mkdir -p "$KEYCHAIN_DIR"
  work="$(mktemp -d "$TMPDIR/koan-signing.XXXXXX")"
  # shellcheck disable=SC2064
  trap "rm -rf '$work'" EXIT

  # A self-signed certificate valid for code signing, 10 years.
  /usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
    -keyout "$work/key.pem" -out "$work/cert.pem" \
    -subj "/CN=$NAME" \
    -addext "basicConstraints=critical,CA:false" \
    -addext "keyUsage=critical,digitalSignature" \
    -addext "extendedKeyUsage=critical,codeSigning" 2>/dev/null
  /usr/bin/openssl pkcs12 -export -inkey "$work/key.pem" -in "$work/cert.pem" \
    -name "$NAME" -out "$work/identity.p12" -passout "pass:$KEYCHAIN_PASSWORD"

  if [[ ! -f $KEYCHAIN ]]; then
    security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
  fi
  # No auto-lock timeout.
  security set-keychain-settings "$KEYCHAIN"
  security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
  security import "$work/identity.p12" -k "$KEYCHAIN" \
    -P "$KEYCHAIN_PASSWORD" -T /usr/bin/codesign
  # So that codesign can use the key without asking each time.
  security set-key-partition-list -S apple-tool:,apple: -s \
    -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN" >/dev/null
  echo "created $NAME ($(identity_hash)) in $KEYCHAIN" >&2
}

case "${1:-}" in
  status)
    hash="$(identity_hash)"
    [[ -n $hash ]] || exit 1
    echo "$hash"
    ;;
  create)
    create
    ;;
  *)
    echo "usage: $0 status|create" >&2
    exit 2
    ;;
esac
