#!/bin/bash
# usage: run.sh <gamedir> <logname> [extra unity args]   (runs up to 15 min, in the background)
cd "$1"; shift; LOG=/tmp/$1.log; shift
exec timeout 900 ./GoneHome.x86_64 -screen-fullscreen 0 -screen-width 640 -screen-height 480 -logFile "$LOG" "$@"
