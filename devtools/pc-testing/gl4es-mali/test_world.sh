#!/bin/bash
# usage: test_world.sh <name> [player args]   (env vars pass through to gl4es)
# Fresh profile -> new game -> gameplay; prints the mean brightness of the world area (0 = black).
N=$1; shift
G=~/ittledew/game; D="/mnt/c/Claude/Ittle Dew/pctest"
pkill -x IttleDew.x86_64; pkill -x Xvfb; pkill -x openbox; sleep 1
rm -rf ~/.config/unity3d/Ludosity; rm -f ~/ittledew/unity.log
Xvfb :99 -screen 0 640x480x24 >/dev/null 2>&1 &
sleep 1; export DISPLAY=:99; openbox >/dev/null 2>&1 &
sleep 1; cd "$G"
GLENV="LD_LIBRARY_PATH=$HOME/gl4es-mali/lib GL4ES_MALI_PROFILE=1 LIBGL_ES=2 LIBGL_GL=21 LIBGL_NOTEST=1 LIBGL_LOGSHADERERROR=1"
[ "$NATIVE" = 1 ] && GLENV="NATIVE=1"
env $GLENV ./IttleDew.x86_64 -logFile ~/ittledew/unity.log -screen-fullscreen 1 -screen-width 640 -screen-height 480 "$@" > ~/ittledew/gl4es_$N.txt 2>&1 &
for i in $(seq 90); do grep -q "Unloading" ~/ittledew/unity.log 2>/dev/null && break; sleep 1; done; sleep 3
K=~/ittledew/key.sh
for i in $(seq 13); do $K z >/dev/null 2>&1; sleep 1.5; done; sleep 5
[ -n "$WALK" ] && { for k in $WALK; do ~/ittledew/key.sh $k 0.8 >/dev/null 2>&1; sleep 0.5; done; }
[ -n "$AFTER" ] && { eval "$AFTER"; }
[ "$PIX" = 1 ] && { touch /tmp/pixlog; sleep 2; rm -f /tmp/pixlog; }
xwd -root -silent | convert xwd:- "$D/w_$N.png"
pgrep -x IttleDew.x86_64 >/dev/null || echo "$N: DEAD"
echo "$N: world brightness $(convert "$D/w_$N.png" -crop 400x160+120+110 -colorspace gray -format '%[fx:int(mean*255)]' info:)"
ls -la ~/.config/unity3d/Ludosity/IttleDew/IttleDew/ | tail -n +2
pkill -x IttleDew.x86_64; sleep 1; pkill -x openbox; pkill -x Xvfb
