#!/bin/bash
# EXAMPLE from a finished port (paths, names and DEVICE_IP are that project's): copy and adapt.
# TEST ONLY, runs on the RG Cube XX (Knulli) over the shared SSH socket: a driving session. The game keeps running
# between calls, so a route can be walked step by step (keys through the uinput keyboard) with a screenshot and
# the fps lines after each step.
#   cube_drive.sh start [fresh|save] [autoload]   stop EmulationStation, park the user's save (fresh profile, or a copy of the
#                                      user's save as the test profile), launch the installed launcher with the
#                                      fpslog preload, wait for the title screen
#   cube_drive.sh keys <spec>...       cube_uinput.py specs (key:count:interval[:hold], keys joined with +)
#   cube_drive.sh shot                 raw framebuffer page to stdout (720x720 BGRA, 2073600 bytes)
#   cube_drive.sh fps [n]              the last n fps lines, RSS, MemAvailable, loaded-object count
#   cube_drive.sh log [n]              the last n lines of unity.log without the fps lines
#   cube_drive.sh stop                 quit, restore the user's save, restart EmulationStation
P=/userdata/roms/ports/ittledew; L=$P/unity.log
game_pid() { for d in /proc/[0-9]*; do [ "$(cat $d/comm 2>/dev/null)" = "IttleDew.x86_64" ] && { echo ${d#/proc/}; return; }; done; }
case "$1" in
  start)
    p=$(game_pid); [ -n "$p" ] && kill -9 $p; pkill -f westonwrap 2>/dev/null; sleep 1
    /etc/init.d/S31emulationstation stop >/dev/null 2>&1; sleep 2
    [ -d $P/conf/.config.user ] || mv $P/conf/.config $P/conf/.config.user 2>/dev/null
    rm -rf $P/conf/.config
    [ "$2" = save ] && [ -d $P/conf/.config.user ] && cp -r $P/conf/.config.user $P/conf/.config
    M=$P/gamedata/IttleDew_Data/Managed   # "autoload": test DLL whose first scene loads save slot 1 (no title, no intro)
    if [ "$3" = autoload ]; then [ -f $M/Assembly-CSharp.dll.release ] || cp $M/Assembly-CSharp.dll $M/Assembly-CSharp.dll.release; cp $P/Assembly-CSharp.autoload.dll $M/Assembly-CSharp.dll; fi
    sed -e 's|BOX64_LD_PRELOAD=libsysvsem.so |BOX64_LD_PRELOAD=libsysvsem.so:libfpslog.so '"$DRIVE_ENV"' |' "${DRIVE_LAUNCHER:-/userdata/roms/ports/Ittle Dew.sh}" > /tmp/idlaunch.sh
    rm -f $L; cd /userdata/roms/ports; T0=$(date +%s)
    env HOME=/userdata/system XDG_RUNTIME_DIR=/var/run PATH=/sbin:/usr/sbin:/bin:/usr/bin SDL_NOMOUSE=1 \
      setsid nohup bash /tmp/idlaunch.sh > /tmp/idrun.txt 2>&1 < /dev/null &
    for i in $(seq 40); do grep -q "Loaded Objects now" $L 2>/dev/null && break; sleep 3; done
    echo "title after $(( $(date +%s) - T0 )) s, game pid $(game_pid), profile ${2:-fresh}"
    ;;
  keys) shift; python3 /tmp/cube_uinput.py "$@" && echo "keys done: $*";;
  newgame)   # from the title: press A every 2 s until a scene load shows in the log (Start -> profile -> new game), then wait for the world
    n0=$(grep -a -c "Loaded Objects now" $L); i=0
    while [ $i -lt 12 ]; do i=$((i + 1)); UINPUT_SETTLE=0.5 python3 /tmp/cube_uinput.py z:1:0.1; sleep 2
      [ "$(grep -a -c "Loaded Objects now" $L)" -gt "$n0" ] && break; done
    echo "new game after $i presses"
    for j in $(seq 75); do grep -q "Loaded Objects now: [5-9][0-9][0-9][0-9]" $L && break; sleep 2; done
    echo "world: $(grep -a -o 'Loaded Objects now: [5-9][0-9][0-9][0-9]' $L | tail -1) after $((j * 2)) s";;
  wait)   # wait <grep pattern> [timeout s]: until unity.log matches (e.g. "Loaded Objects now: [5-9][0-9][0-9][0-9]")
    T0=$(date +%s); for i in $(seq $(( ${3:-120} / 2 ))); do grep -q "$2" $L 2>/dev/null && break; sleep 2; done
    echo "waited $(( $(date +%s) - T0 )) s for '$2': $(grep -a -o "$2" $L | tail -1)";;
  shot) head -c 2073600 /dev/fb0;;
  fps)
    p=$(game_pid)
    grep -a -o '\[fps\][^)]*)' $L | tail -n ${2:-3}
    echo "RSS $(awk '/VmRSS/{print int($2/1024)}' /proc/$p/status 2>/dev/null) MB, avail $(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo) MB, $(grep -a -o 'Loaded Objects now: [0-9]*' $L | tail -1), pid $p"
    ;;
  log) grep -a -v '\[fps\]' $L | tail -n ${2:-15};;
  stop)
    p=$(game_pid); [ -n "$p" ] && kill $p
    for i in $(seq 15); do [ -z "$(game_pid)" ] && ! pgrep -f westonwrap >/dev/null && break; sleep 2; done
    p=$(game_pid); [ -n "$p" ] && kill -9 $p; pkill -f westonwrap 2>/dev/null; sleep 1
    cp $L /tmp/unity_drive.log 2>/dev/null
    rm -rf $P/conf/.config; [ -d $P/conf/.config.user ] && mv $P/conf/.config.user $P/conf/.config
    M=$P/gamedata/IttleDew_Data/Managed; [ -f $M/Assembly-CSharp.dll.release ] && mv $M/Assembly-CSharp.dll.release $M/Assembly-CSharp.dll
    /etc/init.d/S31emulationstation start >/dev/null 2>&1 &
    echo "stopped, save restored: $(ls -d $P/conf/.config 2>/dev/null)"
    ;;
  *) echo "usage: start [fresh|save] | keys spec... | shot | fps [n] | log [n] | stop";;
esac
