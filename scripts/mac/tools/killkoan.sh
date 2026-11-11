#!/bin/bash
# killkoan.sh <pid> : SIGTERM only if that exact pid is a Koan of mine (under ~/work/koan-mac, Contents/MacOS/Koan). Never by name; never anything else.
pid="$1"; [[ "$pid" =~ ^[0-9]+$ ]] || { echo "bad pid"; exit 2; }
cmd=$(ps -p "$pid" -o command= 2>/dev/null | tr 'A-Z' 'a-z')
case "$cmd" in
  */koan-mac/*contents/macos/koan\ *|*/koan-mac/*contents/macos/koan) kill -TERM "$pid" && echo "terminated Koan $pid";;
  "") echo "pid $pid is not running";;
  *) echo "REFUSED: pid $pid is not my Koan: ${cmd:0:80}"; exit 3;;
esac
