#!/bin/bash
# vtabs.sh <build label> <app path on the Mac>
# Runs scripts/mac/drv/vtabs.mjs against a Koan build on the Mac bench (see README.md).
# env: MODE=main|old|nocompact   PREFS=<Preferences json seeded into the profile>   FLAGS=<extra browser flags>
# env: KOAN_MAC_HOST, KOAN_MAC_USER, KOAN_MAC_KEY (or scripts/mac/local.env; see local.env.example)
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -f "$HERE/local.env" ]; then
  # The environment wins over the file.
  while IFS='=' read -r k v; do
    case "$k" in KOAN_MAC_HOST|KOAN_MAC_USER|KOAN_MAC_KEY)
      v="${v%%[[:space:]]#*}"; v="${v%"${v##*[![:space:]]}"}"
      [ -n "${!k:-}" ] || export "$k=$v" ;;
    esac
  done < "$HERE/local.env"
fi
for k in KOAN_MAC_HOST KOAN_MAC_USER KOAN_MAC_KEY; do
  if [ -z "${!k:-}" ]; then
    echo "vtabs.sh: $k is not set. Set KOAN_MAC_HOST, KOAN_MAC_USER and KOAN_MAC_KEY in the environment" >&2
    echo "or in $HERE/local.env (copy local.env.example); nothing about the Mac is kept in the repo." >&2
    exit 2
  fi
done
KEY="${KOAN_MAC_KEY/#\~/$HOME}"
TARGET="$KOAN_MAC_USER@$KOAN_MAC_HOST"
ssh -O check -S /tmp/koan-cm.sock "$TARGET" 2>/dev/null || ssh -i "$KEY" -o BatchMode=yes -M -S /tmp/koan-cm.sock -fN -L 9340:localhost:9340 -L 9341:localhost:9341 "$TARGET"
cd "$HERE/drv" && UI_PORT=9340 CDP_PORT=9341 BUILD="$1" APP_PATH="$2" node vtabs.mjs
