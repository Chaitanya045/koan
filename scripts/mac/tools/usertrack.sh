#!/bin/bash
# usertrack.sh: daemon. Writes tmp/last-user-input.ts = the epoch of the last input that was NOT mine.
# My input tools (keypress2, ptr) write tmp/own-input.ts when they run; an input within 3 s of that is mine, anything else is the user's (or zen-ref's).
M=/Users/chaitanya/work/koan-mac; U=$M/tmp/last-user-input.ts
[ -e $U ] || date +%s > $U       # unknown history: assume the user was here when this started
prev=999999
while true; do
  I=$($M/tmp/idle.sh 2>/dev/null); now=$(date +%s)
  if [ -n "$I" ] && [ "$I" -le 2 ] && [ "$prev" -gt 2 ]; then
    own=$(cat $M/tmp/own-input.ts 2>/dev/null || echo 0)
    if [ $((now - own)) -gt 4 ]; then echo $((now - I)) > $U; fi
  fi
  [ -n "$I" ] && prev=$I
  sleep 1
done
