#!/usr/bin/env bash
# macOS CI: a temporary keychain with Koan's release signing identity.
#
#   scripts/ci/mac_codesign.sh setup               the real certificate
#   scripts/ci/mac_codesign.sh setup --throwaway   a made-up one (no secrets)
#   scripts/ci/mac_codesign.sh teardown
#
# Tagged releases are signed with one fixed self-signed certificate, "Koan
# Release Signing", so macOS sees every release as the same app (a stable
# keychain identity across in-app updates). There is no Developer ID and no
# notarization. The nightly stays ad hoc and never calls this script.
#
# setup (real): KOAN_CODESIGN_P12 (base64 of the .p12) and
#   KOAN_CODESIGN_P12_PASSWORD, the secrets of the protected "release"
#   environment. It makes a keychain under $RUNNER_TEMP with a random
#   password, imports the identity (codesign may use its key without a
#   prompt), puts the keychain on the user's search list, and fails unless
#   the identity's SHA-1 is the one in scripts/ci/codesign_identity.txt.
#   Writes identity=<sha1> and keychain=<path> to $GITHUB_OUTPUT (when set)
#   and prints them; mac_package.sh takes them as KOAN_CODESIGN_IDENTITY and
#   KOAN_CODESIGN_KEYCHAIN.
# setup --throwaway: the same with a certificate made on the spot (RSA 3072,
#   code signing only) and no fingerprint check: for the checks that have no
#   secrets (mac-signing-check.yml, the packaging check of macos-chain.yml).
# teardown: puts the search list back and deletes the keychain (and the trust
#   setup added, see below). Safe when setup failed or never ran.
#
# The .p12 must be made with 3DES/SHA-1 (openssl pkcs12 -export -keypbe
# PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg sha1): macOS's `security
# import` cannot read the AES/SHA-256 files OpenSSL 3 writes by default.
#
# Trust: the certificate is self-signed, so macOS does not trust it. codesign
# signs with an untrusted identity found by its SHA-1 (it only warns that it
# cannot build the chain), but that is not documented, so setup probes it:
# it signs a copy of /usr/bin/true, and only if that fails does it trust the
# certificate for code signing (security add-trusted-cert -p codeSign: in the
# admin domain through `sudo -n`, which CI runners allow and which needs no
# dialog, else in the user domain), and probes again. teardown removes that
# trust. A signed app verifies (codesign --verify --strict) without any
# trust: that only checks the seal.
#
# Secrets: never printed, no `set -x`; the .p12 is written with umask 077
# and deleted right after the import.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STATE="${RUNNER_TEMP:-${TMPDIR:-/tmp}}/koan-codesign"
EXPECTED_FILE="$ROOT/scripts/ci/codesign_identity.txt"

usage() {
  echo "usage: $0 setup [--throwaway] | teardown" >&2
  exit 2
}

[[ $(uname -s) == Darwin ]] || { echo "mac_codesign.sh only runs on macOS" >&2; exit 1; }

# The identity's SHA-1 in <keychain> (a made-up certificate and a real one
# both show up: no -v, which only lists trusted identities).
find_identity() {
  security find-identity -p codesigning "$1" |
    sed -n 's/^ *[0-9]*) \([0-9A-F]\{40\}\) ".*/\1/p' | sort -u
}

# Signs a copy of /usr/bin/true with the identity.
probe_sign() {
  local dir rc=0
  dir="$(mktemp -d "$STATE/probe.XXXXXX")"
  cp /usr/bin/true "$dir/probe"
  codesign --force --sign "$1" --keychain "$2" --timestamp=none \
    "$dir/probe" > "$dir/log" 2>&1 || rc=$?
  if ((rc != 0)); then sed 's/^/codesign: /' "$dir/log" >&2; fi
  rm -rf "$dir"
  return "$rc"
}

setup() {
  local throwaway=0
  case "${1:-}" in
    "") ;;
    --throwaway) throwaway=1 ;;
    *) usage ;;
  esac
  if [[ -e $STATE/keychain ]]; then
    echo "mac_codesign.sh setup already ran (run teardown first)" >&2
    exit 1
  fi
  rm -rf "$STATE"
  (umask 077 && mkdir -p "$STATE")
  # Everything below is created readable by this user only.
  umask 077

  local p12="$STATE/identity.p12" pass
  if ((throwaway)); then
    pass="$(openssl rand -hex 16)"
    cat > "$STATE/openssl.cnf" <<'EOF'
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = Koan Throwaway Signing
[ext]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = critical,codeSigning
EOF
    openssl req -x509 -newkey rsa:3072 -nodes -days 2 \
      -config "$STATE/openssl.cnf" \
      -keyout "$STATE/key.pem" -out "$STATE/cert.pem" 2> /dev/null
    # 3DES/SHA-1, which `security import` can read. -macalg is missing in
    # old LibreSSL (the system openssl): its default is already SHA-1.
    if ! openssl pkcs12 -export -inkey "$STATE/key.pem" -in "$STATE/cert.pem" \
        -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg sha1 \
        -password "pass:$pass" -out "$p12" 2> /dev/null; then
      openssl pkcs12 -export -inkey "$STATE/key.pem" -in "$STATE/cert.pem" \
        -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES \
        -password "pass:$pass" -out "$p12"
    fi
    rm -f "$STATE/key.pem"
  else
    : "${KOAN_CODESIGN_P12:?KOAN_CODESIGN_P12 is not set}"
    : "${KOAN_CODESIGN_P12_PASSWORD:?KOAN_CODESIGN_P12_PASSWORD is not set}"
    pass="$KOAN_CODESIGN_P12_PASSWORD"
    # macOS's base64 takes -D (-d on newer releases); the secret may be
    # wrapped, the decoder skips newlines.
    printf '%s' "$KOAN_CODESIGN_P12" | { base64 -D 2> /dev/null || base64 -d; } > "$p12"
  fi

  local kc="$STATE/koan-signing.keychain-db" kcpass
  kcpass="$(openssl rand -hex 24)"
  # The search list as it was, for teardown.
  security list-keychains -d user |
    sed 's/^[[:space:]]*"//; s/"[[:space:]]*$//' > "$STATE/search-list"
  echo "$kc" > "$STATE/keychain"
  security create-keychain -p "$kcpass" "$kc"
  # No auto-lock: the build job runs for minutes.
  security set-keychain-settings "$kc"
  security unlock-keychain -p "$kcpass" "$kc"
  # `security` takes passwords only as arguments; acceptable on a single-use
  # runner, where no other user's processes can read them.
  security import "$p12" -k "$kc" -f pkcs12 -P "$pass" -T /usr/bin/codesign > /dev/null
  rm -f "$p12"
  # So that codesign uses the key without asking (a dialog on a runner
  # would hang the job).
  security set-key-partition-list -S apple-tool:,apple:,codesign: -s \
    -k "$kcpass" "$kc" > /dev/null
  # In front of the user's own keychains, which stay on the list.
  local list=("$kc") line
  while IFS= read -r line; do
    [[ -z $line ]] || list+=("$line")
  done < "$STATE/search-list"
  security list-keychains -d user -s "${list[@]}"

  local ids identity
  ids="$(find_identity "$kc")"
  if [[ $(printf '%s\n' "$ids" | grep -c .) != 1 ]]; then
    echo "expected one code signing identity in the .p12, found:" >&2
    security find-identity -p codesigning "$kc" >&2 || true
    exit 1
  fi
  identity="$ids"
  if ((! throwaway)); then
    local want
    want="$(tr -d '[:space:]:' < "$EXPECTED_FILE" | tr '[:lower:]' '[:upper:]')"
    if [[ $identity != "$want" ]]; then
      echo "the .p12's identity is $identity, not $want (scripts/ci/codesign_identity.txt)" >&2
      exit 1
    fi
  fi

  # Unverified: whether this works without trusting the certificate.
  if ! probe_sign "$identity" "$kc"; then
    echo "==> codesign refused the untrusted identity; trusting it for code signing"
    security find-certificate -a -p "$kc" > "$STATE/cert.pem"
    if sudo -n true 2> /dev/null; then
      sudo -n security add-trusted-cert -d -r trustRoot -p codeSign \
        -k "$kc" "$STATE/cert.pem"
      echo admin > "$STATE/trust"
    else
      security add-trusted-cert -r trustRoot -p codeSign -k "$kc" "$STATE/cert.pem"
      echo user > "$STATE/trust"
    fi
    probe_sign "$identity" "$kc" || { echo "codesign cannot use $identity" >&2; exit 1; }
  fi

  echo "identity=$identity"
  echo "keychain=$kc"
  if [[ -n ${GITHUB_OUTPUT:-} ]]; then
    {
      echo "identity=$identity"
      echo "keychain=$kc"
    } >> "$GITHUB_OUTPUT"
  fi
}

teardown() {
  [[ -d $STATE ]] || exit 0
  local kc="" line list=()
  [[ ! -f $STATE/keychain ]] || kc="$(cat "$STATE/keychain")"
  if [[ -f $STATE/trust && -f $STATE/cert.pem ]]; then
    if [[ $(cat "$STATE/trust") == admin ]]; then
      sudo -n security remove-trusted-cert -d "$STATE/cert.pem" || true
    else
      security remove-trusted-cert "$STATE/cert.pem" || true
    fi
  fi
  if [[ -f $STATE/search-list ]]; then
    while IFS= read -r line; do
      [[ -z $line ]] || list+=("$line")
    done < "$STATE/search-list"
    if ((${#list[@]})); then
      security list-keychains -d user -s "${list[@]}" || true
    fi
  fi
  if [[ -n $kc ]]; then
    security delete-keychain "$kc" 2> /dev/null || true
  fi
  rm -rf "$STATE"
}

case "${1:-}" in
  setup) shift; setup "$@" ;;
  teardown) teardown ;;
  *) usage ;;
esac
