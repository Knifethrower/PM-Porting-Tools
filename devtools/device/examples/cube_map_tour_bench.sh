#!/bin/bash
# EXAMPLE from a finished port (paths, names and DEVICE_IP are that project's): copy and adapt.
# Device test on the RG Cube XX (Knulli), run through pctest/dev.sh:
#   pctest/dev.sh "bash -s start" < pctest/device_bench.sh   (returns at once)
#   pctest/dev.sh "bash -s status" < pctest/device_bench.sh
#   pctest/dev.sh "bash -s stop" < pctest/device_bench.sh    (restores autoexec.cfg, restarts ES)
# start: stops EmulationStation, adds a map tour (20 s per map, a HUD screenshot each; autoexec.cfg
# and data/default_map_settings.cfg, restored by stop), runs a /tmp copy of Cube.sh (fps: the HUD in the screenshots).
G=/userdata/roms/ports/cube
MAPS="${MAPS:-ruins aard3 douze castle}"
case "$1" in
start)
  /etc/init.d/S31emulationstation stop >/dev/null 2>&1
  cp "$G/autoexec.cfg" /tmp/autoexec.cfg.orig
  # startmap() clears pending sleeps, so each map load (default_map_settings.cfg runs after it)
  # calls benchnext, which screenshots after 20 s, redefines itself and loads the next map.
  cp "$G/data/default_map_settings.cfg" /tmp/dms.cfg.orig
  body="sleep 20000 [ screenshot; quit ]"
  for m in $(echo $MAPS | tr ' ' '\n' | tac); do body="sleep 20000 [ screenshot; alias benchnext [ $body ]; map $m ]"; done
  echo "alias benchnext [ $body ]" >> "$G/autoexec.cfg"
  echo "benchnext" >> "$G/data/default_map_settings.cfg"
  # BIN=cube.aarch64.prof (in the port folder) runs a test binary instead of the port's
  # PRE=/path/glcount.so preloads a probe (draw counts and ms per frame every 300 frames)
  sed "s|^\./\$BINARY|${PRE:+LD_PRELOAD=$PRE }./${BIN:-\$BINARY}|" /userdata/roms/ports/Cube.sh > /tmp/Cube_bench.sh
  cd /userdata/roms/ports
  HOME=/userdata/system XDG_RUNTIME_DIR=/var/run setsid nohup bash /tmp/Cube_bench.sh > /tmp/cube_run.txt 2>&1 < /dev/null &
  echo started ;;
status)
  for p in /proc/[0-9]*; do [ "$(cat $p/comm 2>/dev/null)" = cube.aarch64 ] && echo "running pid ${p#/proc/}"; done
  grep -a "fps\|read map" "$G/log.txt" | tail -5
  cat /proc/asound/card*/pcm*p/sub*/status 2>/dev/null | grep -a state ;;
stop)
  for p in /proc/[0-9]*; do [ "$(cat $p/comm 2>/dev/null)" = cube.aarch64 ] && kill "${p#/proc/}"; done
  sleep 2
  [ -f /tmp/autoexec.cfg.orig ] && cp /tmp/autoexec.cfg.orig "$G/autoexec.cfg" && rm /tmp/autoexec.cfg.orig
  [ -f /tmp/dms.cfg.orig ] && cp /tmp/dms.cfg.orig "$G/data/default_map_settings.cfg" && rm /tmp/dms.cfg.orig
  /etc/init.d/S31emulationstation start >/dev/null 2>&1 &
  echo stopped ;;
esac
