#!/bin/bash
# inputok.sh <pid>: exit 0 only if the screen is granted AND the frontmost app is exactly <pid> (my own Koan). Used before every posted key / pointer event.
M="${KOAN_MAC_ROOT:-$HOME/work/koan-mac}"
"$M/tmp/grantok.sh" || exit 7
[ "$("$M/tmp/frontpid.sh")" = "$1" ] || exit 8
