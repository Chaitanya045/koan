#!/bin/bash
# prints the seconds since the last input that was not mine
M="${KOAN_MAC_ROOT:-$HOME/work/koan-mac}"
echo $(( $(date +%s) - $(cat "$M/tmp/last-user-input.ts" 2>/dev/null || echo 0) ))
