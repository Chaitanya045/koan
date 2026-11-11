#!/bin/bash
# frontpid.sh: pid of the frontmost app
lsappinfo info -only pid "$(lsappinfo front)" 2>/dev/null | sed -n 's/.*pid = \([0-9]*\).*/\1/p' | head -1
