#!/bin/bash
# vtabs.sh <build label> <app path on the Mac>
# Runs scripts/mac/drv/vtabs.mjs against a Koan build on the Mac bench (see README.md).
# env: MODE=main|old|nocompact   PREFS=<Preferences json seeded into the profile>   FLAGS=<extra browser flags>
HERE="$(cd "$(dirname "$0")" && pwd)"
ssh -O check -S /tmp/koan-cm.sock chaitanya@chaitanyas-macbook-air 2>/dev/null || ssh -i ~/.ssh/koan_mac_ed25519 -o BatchMode=yes -M -S /tmp/koan-cm.sock -fN -L 9340:localhost:9340 -L 9341:localhost:9341 chaitanya@chaitanyas-macbook-air
cd "$HERE/drv" && UI_PORT=9340 CDP_PORT=9341 BUILD="$1" APP_PATH="$2" node vtabs.mjs
