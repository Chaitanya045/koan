#!/bin/bash
# inputok.sh <pid>: exit 0 only if the screen is granted AND the frontmost app is exactly <pid> (my own Koan). Used before every posted key / pointer event.
/Users/chaitanya/work/koan-mac/tmp/grantok.sh || exit 7
[ "$(/Users/chaitanya/work/koan-mac/tmp/frontpid.sh)" = "$1" ] || exit 8
