#!/bin/bash
# Records a Unity 4 player's GL calls with apitrace, for replay_compare.sh (Mesa vs gl4es on the same calls).
# Recorded on Mesa told to be GL 2.1 / GLSL 1.20 (LIB=ref, default), so Unity takes the GL 2.1 paths it takes on
# gl4es and every call is valid desktop GL. LIB=ship records through gl4es instead, but apitrace then writes
# client vertex arrays as size 0 / type 0 (it reads them back with glGet, and gl4es answers 0).
#   trace_unity.sh <game> <name> "<key script>"
#   game: ittledew | teslagrad | <path to player binary>
#   key script words: k:<key>[:<hold s>]  w:<sec>  (the game window gets the keys)
# Output: $GT/traces/<name>.trace (+ .log). Own Xvfb (:98) and openbox; kills only its own processes.
HERE=$(cd "$(dirname "$0")/.." && pwd); . "$HERE/env.sh"
GAME=$1; NAME=$2; KS=$3; RES=${RES:-640x480}; LIB=${LIB:-ref}
case $GAME in
    ittledew)  BIN=$HOME/ittledew/game/IttleDew.x86_64; PROFILE=$HOME/.config/unity3d/Ludosity; WIN=IttleDew ;;
    teslagrad) BIN=$HOME/teslagrad/game/Teslagrad; PROFILE=$HOME/.config/unity3d/Rain; WIN=Teslagrad ;;
    *)         BIN=$GAME; PROFILE=""; WIN=$(basename "$GAME") ;;
esac
OUT=$GT/traces; mkdir -p "$OUT"
[ -n "$PROFILE" ] && [ "$KEEP" != 1 ] && rm -rf "$PROFILE"
$GT/bin/gtXvfb :98 -screen 0 ${RES}x24 -nolisten tcp >/dev/null 2>&1 & XP=$!
sleep 1; export DISPLAY=:98; openbox >/dev/null 2>&1 & OP=$!
sleep 1; cd "$(dirname "$BIN")"
WRAP=/usr/lib/x86_64-linux-gnu/apitrace/wrappers/glxtrace.so
rm -f "$OUT/$NAME.trace"
if [ "$LIB" = ref ]; then
    # Mesa as GL 2.1 without the extensions the shipped gl4es lacks (else Unity uses e.g. glFenceSync)
    OVR=$(bash "$HERE/trace/ext_override.sh" ${EXTOF:-ship}); echo "extensions turned off: $OVR" > "$OUT/$NAME.exts"
    GLENV="$(gl4es_env ref) MESA_GL_VERSION_OVERRIDE=2.1 MESA_GLSL_VERSION_OVERRIDE=120"
else GLENV="$(gl4es_env $LIB) TRACE_LIBGL=$GT/lib/$LIB/libGL.so.1"; fi
env $GLENV ${OVR:+MESA_EXTENSION_OVERRIDE="$OVR"} TRACE_FILE="$OUT/$NAME.trace" LD_PRELOAD=$WRAP \
    "./$(basename "$BIN")" -logFile "$OUT/$NAME.unity.log" -screen-fullscreen 1 -screen-width ${RES%x*} -screen-height ${RES#*x} \
    > "$OUT/$NAME.log" 2>&1 & GP=$!
sleep ${START:-25}
for w in $KS; do
    case $w in
        k:*) IFS=: read _ k t <<< "$w"; W=$(xdotool search --name "$WIN" | head -1)
             xdotool keydown --window "$W" "$k"; sleep "${t:-0.15}"; xdotool keyup --window "$W" "$k"; sleep 0.3 ;;
        w:*) sleep "${w#w:}" ;;
    esac
    kill -0 $GP 2>/dev/null || { echo "$NAME: game exited early"; break; }
done
kill $GP 2>/dev/null; sleep 2; kill -9 $GP 2>/dev/null; kill $OP $XP 2>/dev/null
ls -la "$OUT/$NAME.trace"
apitrace info "$OUT/$NAME.trace" 2>/dev/null | grep -iE "frames|calls" | head -3
