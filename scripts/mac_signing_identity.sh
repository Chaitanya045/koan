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
# certificate in a keychain of its own, $KOAN_MAC/keychains/koan-signing.keychain-db
# (KOAN_SIGNING_KEYCHAIN overrides the path; scripts/mac_signing_check.sh uses
# that for a throw-away one). It is not the login keychain and not trusted
# system-wide: it only signs builds on this Mac. `security create-keychain`
# always adds the new keychain to the user's keychain search list, so
# `create` records the list first and puts it back exactly as it was (minus
# any earlier copy of this keychain) when it finishes or fails. Nothing here
# deletes or changes an existing keychain item or keychain; `create` makes
# one new keychain file, and removes that file again only if this run made it
# and then failed.
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
KEYCHAIN="${KOAN_SIGNING_KEYCHAIN:-$KEYCHAIN_DIR/koan-signing.keychain-db}"
# The keychain holds one key that only signs local builds; its password
# protects nothing worth guarding, and codesign needs the keychain unlocked.
KEYCHAIN_PASSWORD="koan-local-signing"

# The certificate's SHA-1 hash, or nothing.
identity_hash() {
  [[ -f $KEYCHAIN ]] || return 0
  # A keychain file without the certificate (an earlier run failed) is "no
  # identity", not an error.
  { security find-certificate -c "$NAME" -Z "$KEYCHAIN" 2>/dev/null || true; } |
    sed -n 's/^SHA-1 hash: //p' | head -1
}

# The user's keychain search list, one path per line, without this keychain.
search_list() {
  { security list-keychains -d user || return 1; } |
    sed -e 's/^[[:space:]]*"//' -e 's/"[[:space:]]*$//' |
    grep -v '/koan-signing[^/]*$' || true
}

# Globals, not locals: the EXIT trap runs after create() has returned.
had_keychain=0
ok=0
saved=()
work=""

create() {
  local hash cnf line
  hash="$(identity_hash)"
  if [[ -n $hash ]]; then
    echo "$NAME already exists ($hash)" >&2
    return 0
  fi
  # Everything private is 0600/0700, including the unencrypted key on disk.
  umask 077

  # create-keychain adds the new keychain to the search list; record the list
  # now and put it back exactly. Refuse to go on if it can't be read.
  while IFS= read -r line; do
    [[ -n $line ]] && saved+=("$line")
  done < <(search_list)
  if [[ ${#saved[@]} -eq 0 ]]; then
    if [[ -n ${KOAN_SIGNING_KEYCHAIN:-} ]]; then
      # A throw-away keychain at an explicit path (scripts/mac_signing_check.sh
      # selftest) in a session without a user keychain domain, e.g. ssh: there
      # is no search list to protect, and the path is not the user's.
      echo "no user keychain search list here (ssh?); using only $KEYCHAIN" >&2
    else
      echo "can't read the keychain search list (security list-keychains -d user); not creating anything" >&2
      return 1
    fi
  fi
  [[ -f $KEYCHAIN ]] && had_keychain=1
  mkdir -p "$(dirname "$KEYCHAIN")"
  work="$(mktemp -d "$TMPDIR/koan-signing.XXXXXX")"
  cnf="$work/openssl.cnf"

  finish() {
    rm -rf "$work"
    if [[ ${#saved[@]} -gt 0 ]]; then
      security list-keychains -d user -s "${saved[@]}" >/dev/null 2>&1 || true
    fi
    if [[ $ok -eq 0 && $had_keychain -eq 0 ]]; then
      # This run made the keychain file and then failed: remove just that file.
      rm -f "$KEYCHAIN"
      echo "failed; removed the new keychain file and restored the search list" >&2
    fi
  }
  trap finish EXIT INT TERM

  # A self-signed certificate valid for code signing, 10 years. A config file
  # instead of -addext, which older LibreSSL (macOS's /usr/bin/openssl) lacks.
  cat >"$cnf" <<EOF
[req]
distinguished_name = dn
prompt = no
x509_extensions = ext
[dn]
CN = $NAME
[ext]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = critical,codeSigning
EOF
  /usr/bin/openssl req -x509 -new -newkey rsa:2048 -nodes -sha256 -days 3650 \
    -config "$cnf" -keyout "$work/key.pem" -out "$work/cert.pem"
  # Algorithms `security import` reads on every macOS (3DES/SHA-1); OpenSSL 3's
  # defaults (AES, PBKDF2) fail there with "MAC verification failed". Old
  # LibreSSL may not know -macalg: try without.
  /usr/bin/openssl pkcs12 -export -inkey "$work/key.pem" -in "$work/cert.pem" \
    -name "$NAME" -out "$work/identity.p12" -passout "pass:$KEYCHAIN_PASSWORD" \
    -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg sha1 2>/dev/null ||
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
  hash="$(identity_hash)"
  [[ -n $hash ]] || { echo "the certificate didn't import" >&2; return 1; }
  ok=1
  echo "created $NAME ($hash) in $KEYCHAIN; keychain search list unchanged" >&2
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
