#!/bin/bash
# Device side (runs ON the handheld, fed through dev.sh): start an installed port for a test run
# with the front end stopped, check on it, stop it and bring the front end back.
#   dev.sh "bash -s start '<Port Name>.sh' [sed expression]" < device_run.sh
#   dev.sh "bash -s status <process name> [log file]"       < device_run.sh
#   dev.sh "bash -s stop <process name>"                    < device_run.sh
# start runs a /tmp copy of the launcher (the sed expression edits that copy, e.g. to add a preload
# or env: 's|^\./\$BINARY|LD_PRELOAD=/tmp/glcount.so ./$BINARY|') detached, output in /tmp/port_run.txt.
# The front end: Knulli/Batocera /etc/init.d/S31emulationstation, otherwise systemd
# (emulationstation on dArkOS, essway on ROCKNIX); FE_STOP / FE_START override the commands.
# PORTS (default: the first of /userdata/roms/ports, /roms/ports, /storage/roms/ports) is where the
# launcher is. Ask the user before running this on a device they also use.
# From Cube (pctest/device_bench.sh) and Ittle Dew (pctest/cube_drive.sh).
for d in /userdata/roms/ports /roms/ports /storage/roms/ports; do [ -d "$d" ] && { PORTS=${PORTS:-$d}; break; }; done
if [ -x /etc/init.d/S31emulationstation ]; then
  FE_STOP=${FE_STOP:-"/etc/init.d/S31emulationstation stop"}; FE_START=${FE_START:-"/etc/init.d/S31emulationstation start"}
else
  for s in emulationstation essway; do systemctl is-active -q $s 2>/dev/null && { FE_STOP=${FE_STOP:-"systemctl stop $s"}; FE_START=${FE_START:-"systemctl start $s"}; }; done
fi
# comm holds only the first 15 characters of the name
pids() { for p in /proc/[0-9]*; do [ "$(cat $p/comm 2>/dev/null)" = "${1:0:15}" ] && echo "${p#/proc/}"; done; }
case "$1" in
start)
  [ -f "$PORTS/$2" ] || { echo "no launcher $PORTS/$2"; exit 1; }
  [ -n "$FE_STOP" ] && $FE_STOP >/dev/null 2>&1; sleep 2
  sed "${3:-}" "$PORTS/$2" > /tmp/port_run.sh
  cd "$PORTS"
  HOME=${HOME:-/root} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/var/run} setsid nohup bash /tmp/port_run.sh > /tmp/port_run.txt 2>&1 < /dev/null &
  echo "started $2 (front end: ${FE_STOP:-none})" ;;
status)
  p=$(pids "$2" | head -1)
  if [ -n "$p" ]; then
    echo "running pid $p, RSS $(awk '/VmRSS/{print int($2/1024)}' /proc/$p/status) MB, $(ls /proc/$p/task | wc -l) threads"
  else echo "not running"; fi
  echo "MemAvailable $(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo) MB"
  [ -n "$3" ] && tail -n 8 "$3" ;;
stop)
  for p in $(pids "$2"); do kill "$p"; done; sleep 2
  for p in $(pids "$2"); do kill -9 "$p"; done
  [ -n "$FE_START" ] && { $FE_START >/dev/null 2>&1 & }
  echo "stopped, front end restarted" ;;
*) echo "usage: start '<Port Name>.sh' [sed expr] | status <process> [log] | stop <process>" ;;
esac
