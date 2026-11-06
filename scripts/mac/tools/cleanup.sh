#!/bin/bash
# cleanup.sh: after a run (or on a stop), leave nothing of mine on screen.
# 1. quit my own test Koans by exact pid (only those whose profile is under ~/work/koan-mac/tmp/pass-); 2. dismiss the crash dialogs that name Koan; 3. verify.
M="${KOAN_MAC_ROOT:-$HOME/work/koan-mac}"
for pid in $(ps -Ao pid,command | grep "[M]acOS/Koan -" | grep "user-data-dir=$M/tmp/pass-" | awk '{print $1}'); do $M/tmp/killkoan.sh $pid; done
for i in 1 2 3 4 5 6 7 8; do ps -Ao command | grep "[M]acOS/Koan -" | grep -q "user-data-dir=$M/tmp/pass-" || break; sleep 1; done
sleep 2; for i in 1 2 3 4 5 6; do out=$($M/tmp/crashdlg dismiss | tail -1); echo "$out"; case "$out" in "0 Koan"*) break;; esac; sleep 1.5; done
left=$(( $($M/tmp/winall | grep -c "^Koan ") + $($M/tmp/crashdlg list | tail -1 | cut -d" " -f1) ))
if [ "$left" = "0" ] && ! ps -Ao command | grep "[M]acOS/Koan -" | grep -q "user-data-dir=$M/tmp/pass-"; then echo "CLEAN: no test Koan, no dialog, no Koan window on screen"; else echo "LEFTOVER: $left window(s) on screen:"; $M/tmp/winall | grep "^Koan "; $M/tmp/crashdlg list; ps -Ao pid,command | grep "[M]acOS/Koan -" | grep "user-data-dir=$M/tmp/pass-" | cut -c1-100; fi
