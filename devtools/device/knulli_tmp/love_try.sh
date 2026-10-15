#!/bin/sh
# love_try.sh <WSL dir with main.lua> <png> [LOVE_ENV]: copy a LÖVE game folder from WSL to the
# device SD card (/userdata/lovetry, not the RAM-backed /tmp), run it with love_run.sh (virtual pad env) and take a screenshot after 12 s.
# PC side (Git Bash); needs DEV_HOST/DEV_SOCK as for dev.sh. License: 0BSD.
D=$(dirname "$0")/..
export MSYS_NO_PATHCONV=1
sh "$D/dev.sh" 'rm -rf /userdata/lovetry; mkdir -p /userdata/lovetry'
wsl.exe -e tar cf - --exclude=.git -C "$1" . | sh "$D/dev.sh" 'cd /userdata/lovetry && tar xf - --no-same-owner 2>/dev/null'
sh "$D/dev.sh" "LOVE_ENV='$3' bash /tmp/love_run.sh /userdata/lovetry >/dev/null; sleep 4; grep -iv pulse /tmp/love_run.txt | tail -6"
sh "$D/fbshot.sh" "$2"
