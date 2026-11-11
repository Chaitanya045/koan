#!/bin/bash
echo $(( $(/usr/sbin/ioreg -c IOHIDSystem | awk "/HIDIdleTime/ {print \$NF; exit}") / 1000000000 ))
