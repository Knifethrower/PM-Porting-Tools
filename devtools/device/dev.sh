#!/bin/sh
# Run a command on the handheld through a shared SSH connection that the user opened once per boot
# (no password or key handling here). From Git Bash on the PC:
#   user, once:  wsl ssh -M -S /tmp/<name>.sock -N -o ServerAliveInterval=30 root@<device ip>
#   then:        DEV_HOST=root@<device ip> DEV_SOCK=/tmp/<name>.sock dev.sh '<command>'
#                dev.sh 'bash -s start' < device_run.sh       (feed a script on stdin)
# DEV_HOST is required; DEV_SOCK defaults to /tmp/device.sock. Copying files over the same socket:
#   wsl.exe -e scp -o ControlPath=$DEV_SOCK -o BatchMode=yes <file> $DEV_HOST:/tmp/
# From Cube / Stunt Playground (pctest/dev.sh).
: "${DEV_HOST:?set DEV_HOST=root@<device ip>}"
export MSYS_NO_PATHCONV=1
exec wsl.exe -e ssh -S "${DEV_SOCK:-/tmp/device.sock}" -o BatchMode=yes "$DEV_HOST" "$@"
