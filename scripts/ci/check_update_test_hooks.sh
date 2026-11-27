#!/usr/bin/env bash
# Fails if a finished build has Koan's test-only update hooks
# (koan_update_test_hooks, src/browser/updates/BUILD.gn): the switches that
# point the update check at another manifest and key. Run on every build
# that is published (a release, or a nightly: a run kept as a base) before it
# is packaged.
#
#   scripts/ci/check_update_test_hooks.sh <out dir> <binary or app>
#
# Two proofs: args.gn doesn't set the arg, and the binary doesn't hold the
# switch's name, which only the hooks' code uses.
set -euo pipefail

OUT="${1:-}"
BINARY="${2:-}"
if [[ -z $OUT || -z $BINARY ]]; then
  echo "usage: $0 <out dir> <binary or app>" >&2
  exit 2
fi
if [[ ! -f $OUT/args.gn ]]; then
  echo "no $OUT/args.gn" >&2
  exit 1
fi
if grep -Eq '^[[:space:]]*koan_update_test_hooks[[:space:]]*=[[:space:]]*true' "$OUT/args.gn"; then
  echo "$OUT/args.gn sets koan_update_test_hooks: this build must not be published" >&2
  exit 1
fi
if [[ ! -e $BINARY ]]; then
  echo "no $BINARY" >&2
  exit 1
fi
if grep -r -a -q -F 'koan-update-manifest-url' "$BINARY"; then
  echo "$BINARY has the update test hooks built in: it must not be published" >&2
  exit 1
fi
echo "No update test hooks in $BINARY"
