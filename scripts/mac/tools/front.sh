#!/bin/bash
lsappinfo info -only name "$(lsappinfo front)" 2>/dev/null | head -1
