#!/bin/bash
# TEST ONLY, runs ON the RG Cube XX (Knulli): one A/B run of a Unity 4 port with a given gl4es libGL.so.1.
#   cube_ab.sh install <game> <lib> <tag>   back up the port's own gl4es once (libGL.so.1.orig), put <lib> in place
#   cube_ab.sh run <game> <tag> <seconds> [key specs...]
#                                           stop ES, launch the installed launcher (fpslog preloaded), wait, press the
#                                           keys (cube_uinput.py specs), screenshot to /tmp/ab_<tag>.raw, collect fps /
#                                           memory / errors into /tmp/ab_<tag>.txt, quit the game (SIGTERM, then KILL)
#   cube_ab.sh restore <game>               the port's own gl4es back
#   cube_ab.sh es                           start EmulationStation the way Knulli does (HOME, XDG_RUNTIME_DIR)
# game: teslagrad | ittledew. Never leaves ES stopped: run ends with "es" unless AB_KEEP_ES_STOPPED=1.
G=$2
case "$G" in
    teslagrad) P=/userdata/roms/ports/teslagrad; LAUNCH=/userdata/roms/ports/Teslagrad.sh; COMM=Teslagrad ;;
    ittledew)  P=/userdata/roms/ports/ittledew; LAUNCH="/userdata/roms/ports/Ittle Dew.sh"; COMM=IttleDew.x86_64 ;;
    *) [ "$1" = es ] || { echo "game: teslagrad | ittledew"; exit 1; } ;;
esac
pid() { for d in /proc/[0-9]*; do [ "$(cat $d/comm 2>/dev/null)" = "$COMM" ] && { echo ${d#/proc/}; return; }; done; }
es_start() {
    pgrep -f '^emulationstation( |$)' >/dev/null && return
    env HOME=/userdata/system XDG_RUNTIME_DIR=/var/run setsid /etc/init.d/S31emulationstation start >/dev/null 2>&1 < /dev/null &
    sleep 12; a=$(pgrep -f '^emulationstation( |$)'); sleep 10; b=$(pgrep -f '^emulationstation( |$)')
    [ -n "$a" ] && [ "$a" = "$b" ] && echo "ES up (pid $b, stable)" || echo "ES NOT stable: '$a' -> '$b'"
}
case "$1" in
  install)
    L=$P/gl4es.aarch64/libGL.so.1
    [ -f $L.orig ] || cp $L $L.orig
    cp "$3" $L.new && mv -f $L.new $L
    echo "$G: installed $4 ($(sha1sum $L | cut -c1-8)), original kept as libGL.so.1.orig ($(sha1sum $L.orig | cut -c1-8))";;
  restore)
    L=$P/gl4es.aarch64/libGL.so.1
    [ -f $L.orig ] && mv -f $L.orig $L && echo "$G: original gl4es back ($(sha1sum $L | cut -c1-8))";;
  run)
    TAG=$3; SECS=$4; shift 4
    p=$(pid); [ -n "$p" ] && kill -9 $p
    /etc/init.d/S31emulationstation stop >/dev/null 2>&1; sleep 2
    sed -e 's|MONO_DISABLE_SHM=1 |MONO_DISABLE_SHM=1 BOX64_LD_PRELOAD=/tmp/libfpslog.so |' "$LAUNCH" > /tmp/ab_launch.sh
    rm -f $P/unity.log $P/log.txt; cd /userdata/roms/ports; T0=$(date +%s)
    env HOME=/userdata/system XDG_RUNTIME_DIR=/var/run PATH=/sbin:/usr/sbin:/bin:/usr/bin SDL_NOMOUSE=1 \
        setsid nohup bash /tmp/ab_launch.sh > /tmp/ab_run.txt 2>&1 < /dev/null &
    sleep "$SECS"
    [ $# -gt 0 ] && python3 /tmp/cube_uinput.py "$@" >/dev/null 2>&1
    sleep 2
    head -c 2073600 /dev/fb0 > /tmp/ab_$TAG.raw
    p=$(pid)
    {
      echo "== $G $TAG: gl4es $(sha1sum $P/gl4es.aarch64/libGL.so.1 | cut -c1-8), $(( $(date +%s) - T0 )) s, game pid ${p:-NONE}"
      [ -n "$p" ] && echo "RSS $(awk '/VmRSS/{print int($2/1024)}' /proc/$p/status) MB, avail $(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo) MB"
      echo "-- fps"; grep -a -o '\[fps\][^)]*)' $P/unity.log 2>/dev/null | tail -n 6
      echo "-- errors"; grep -a -iE 'LIBGL: (error|warning)|error|exception|abort|segmentation|signal|crash|invalid' $P/unity.log $P/log.txt /tmp/ab_run.txt 2>/dev/null \
          | grep -v -iE 'fps\]|Error loading|ScreenSelector|Steam' | sort | uniq -c | sort -rn | head -15
      echo "-- log.txt tail"; tail -n 5 $P/log.txt 2>/dev/null
    } > /tmp/ab_$TAG.txt
    [ -n "$p" ] && kill $p; for i in $(seq 10); do [ -z "$(pid)" ] && break; sleep 1; done
    p=$(pid); [ -n "$p" ] && kill -9 $p
    sleep 2; echo "-- after quit" >> /tmp/ab_$TAG.txt; tail -n 3 $P/log.txt >> /tmp/ab_$TAG.txt 2>/dev/null
    cat /tmp/ab_$TAG.txt
    [ "$AB_KEEP_ES_STOPPED" = 1 ] || es_start;;
  es) es_start;;
  *) sed -n 2,11p "$0";;
esac
