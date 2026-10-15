#!/bin/bash
# usage: replay_compare.sh <binary> <outdir>
# Runs <binary> (an x86_64 PC build, from the folder it is in) on Xvfb with the glcount probe
# (../profiling/device-sampler/glcount.c) in FAKECLOCK mode with scripted KEYS, so every run draws
# the same frames; the frames listed in FRAMES are dumped to <outdir>/f<N>.ppm. Compare two runs
# (old/new build, gl4es/native GLES, two screen sizes) with compare_frames.sh.
#   GLCOUNT   the built probe (gcc -O2 -shared -fPIC -o glcount.so glcount.c -ldl); default ~/glcount.so
#   KEYS      "scancode:fromframe-toframe ..." (SDL scancodes: Z 29, X 27, Enter 40, Esc 41, P 19,
#             arrows R79 L80 D81 U82, IJKL 12 13 14 15); default: Z at frame 60 (start)
#   FRAMES    frames to dump (default "60 200 400 700 1000"); DUMPEVERY=n dumps every n-th frame too
#   RES=WxH   screen size (default 640x480)          XDISP  Xvfb display (default 96)
#   CLEAN     files to delete in the run folder first (saved scores change the title screen)
#   ARGS      arguments for the game                 TIMEOUT  seconds to wait (default 240)
#   GL4ES=dir run through an x86_64 gl4es build in dir (Mali profile) instead of
#             SDL's own Mesa GLES path
#   EXTRA_PRELOAD  more preloads after the probe
# From Torus Trooper and Mu-cade (pctest/).
BIN=$(realpath "$1"); OUT=$(realpath -m "$2")
[ -x "$BIN" ] && [ -n "$2" ] || { echo "usage: replay_compare.sh <binary> <outdir>"; exit 1; }
FRAMES=${FRAMES:-"60 200 400 700 1000"}
KEYS=${KEYS:-"29:60-64"}
GLCOUNT=${GLCOUNT:-$HOME/glcount.so}
D=${XDISP:-96}
NAME=$(basename "$BIN")
mkdir -p "$OUT"; rm -f "$OUT"/*.ppm
pkill -f "^\./$NAME( |$)"; pkill -f "Xvfb :$D"; sleep 1
Xvfb :$D -screen 0 ${RES:-640x480}x24 -nolisten tcp >/dev/null 2>&1 &
sleep 1; export DISPLAY=:$D; openbox >/dev/null 2>&1 &
sleep 1
cd "$(dirname "$BIN")"
rm -f $CLEAN
GLENV=""
[ -n "$GL4ES" ] && GLENV="LD_LIBRARY_PATH=$GL4ES SDL_VIDEO_GL_DRIVER=$GL4ES/libGL.so.1 GL4ES_MALI_PROFILE=1 LIBGL_ES=2 LIBGL_GL=21 LIBGL_NOTEST=1"
env $GLENV SDL_AUDIODRIVER=dummy \
  LD_PRELOAD=$GLCOUNT${EXTRA_PRELOAD:+:$EXTRA_PRELOAD} FAKECLOCK=1 KEYS="$KEYS" DUMP="$OUT" DUMPFRAMES="$FRAMES" DUMPEVERY="${DUMPEVERY:-0}" \
  ./"$NAME" $ARGS > "$OUT/log.txt" 2>&1 &
last=${FRAMES##* }
for i in $(seq ${TIMEOUT:-240}); do [ -f "$OUT/f$last.ppm" ] && break; pgrep -f "^\./$NAME( |$)" >/dev/null || break; sleep 1; done
sleep 1
pkill -f "^\./$NAME( |$)"; sleep 1; pkill -f "openbox"; pkill -f "Xvfb :$D"
grep GLCOUNT "$OUT/log.txt" | tail -5
echo "$(ls "$OUT"/*.ppm 2>/dev/null | wc -l) frames in $OUT"
