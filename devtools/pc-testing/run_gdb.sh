#!/bin/bash
# usage: run_gdb.sh <gamedir> <logname>  -- like run_gles.sh, under gdb; backtrace to /tmp/<logname>.gdb
D=$(dirname "$(readlink -f "$0")")
cd "$1"; shift; LOG=/tmp/$1.log; GDBOUT=/tmp/$1.gdb
cp "$D/gles_force.so" /tmp/gles_force.so
exec gdb -batch -ex "set environment LD_PRELOAD=/tmp/gles_force.so" \
  -ex "handle SIGXCPU SIGPWR SIG35 SIG36 SIGSEGV nostop noprint pass" -ex "handle SIGABRT stop print" \
  -ex "run" -ex "bt 30" -ex "info registers rip" \
  --args ./GoneHome.x86_64 -screen-fullscreen 0 -screen-width 640 -screen-height 480 -logFile "$LOG" > "$GDBOUT" 2>&1
