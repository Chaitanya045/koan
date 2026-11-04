#!/bin/bash
# prints the seconds since the last input that was not mine
echo $(( $(date +%s) - $(cat /Users/chaitanya/work/koan-mac/tmp/last-user-input.ts 2>/dev/null || echo 0) ))
