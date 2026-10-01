#!/bin/bash
# usage: run_gles.sh <gamedir> <logname> [extra unity args] -- runs the game with a GLES 3.2 context
D=$(dirname "$(readlink -f "$0")")
cd "$1"; shift; LOG=/tmp/$1.log; shift
cp "$D/gles_force.so" /tmp/gles_force.so
LD_PRELOAD=/tmp/gles_force.so exec timeout 1800 ./GoneHome.x86_64 -screen-fullscreen 0 -screen-width 640 -screen-height 480 -logFile "$LOG" "$@"
