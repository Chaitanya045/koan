#!/bin/bash
# grantok.sh: exit 0 only while the tester has granted the screen (tmp/screen-granted exists). The grant is written by me on an explicit 'screen free' and removed on 'testing'.
[ -e /Users/chaitanya/work/koan-mac/tmp/screen-granted ]
