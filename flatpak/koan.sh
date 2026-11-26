#!/bin/sh
# /app/bin/koan: starts Koan through cobalt (org.chromium.Chromium.BaseApp),
# which reads /app/etc/cobalt.ini, adds the flags from
# ~/.var/app/dev.koan.Koan/config/koan-flags.conf and runs zypak.
exec cobalt "$@"
