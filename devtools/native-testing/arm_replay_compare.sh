#!/bin/bash
# usage: arm_replay_compare.sh <port folder> <binary name> <outdir>
# The aarch64 counterpart of replay_compare.sh: copies <port folder> (e.g. release/<portdir>) into
# an arm64 chroot, builds the glcount probe there, and runs the aarch64 binary through qemu-user
# (the chroot's Mesa GLES) on Xvfb with FAKECLOCK and the scripted KEYS. The dumped frames can be
# compared with the x86_64 ones (compare_frames.sh). Slow (software GLES under qemu): keep FRAMES short.
# Run as root (wsl -u root): it writes into the chroot and uses chroot.
#   SYSROOT   the arm64 chroot (default /home/$USER_NAME/chroot-bullseye, see ../../build/native-aarch64)
#   GLCOUNT_C probe source (default: ../profiling/device-sampler/glcount.c next to this folder)
#   EXTRA     more folders to copy next to the binary (game data kept outside the port folder)
#   KEYS, FRAMES, RES, XDISP, CLEAN, ARGS, TIMEOUT: as in replay_compare.sh (TIMEOUT default 1200)
# From Torus Trooper and Mu-cade (pctest/).
SRC=$(realpath "$1"); NAME=$2; OUT=$(realpath -m "$3")
[ -d "$SRC" ] && [ -n "$NAME" ] && [ -n "$3" ] || { echo "usage: arm_replay_compare.sh <port folder> <binary name> <outdir>"; exit 1; }
HERE=$(dirname "$(realpath "$0")")
S=${SYSROOT:-/home/${USER_NAME:-$(ls /home | head -1)}/chroot-bullseye}
GLCOUNT_C=${GLCOUNT_C:-$HERE/../profiling/device-sampler/glcount.c}
FRAMES=${FRAMES:-"60 200 400"}
KEYS=${KEYS:-"29:60-64"}
D=${XDISP:-95}
R=/run-replay
mkdir -p "$OUT"; rm -f "$OUT"/*.ppm
pkill -f "Xvfb :$D"; sleep 1
Xvfb :$D -screen 0 ${RES:-640x480}x24 -nolisten tcp >/dev/null 2>&1 &
sleep 1; export DISPLAY=:$D; openbox >/dev/null 2>&1 &
sleep 1
mkdir -p "$S$R" "$S/build"
rsync -a --delete "$SRC/" "$S$R/"
for e in $EXTRA; do cp -r "$e" "$S$R/"; done
cp "$GLCOUNT_C" "$S/build/glcount.c"
chroot "$S" gcc -O2 -shared -fPIC -o /build/glcount.so /build/glcount.c -ldl || exit 1
rm -rf "$S$R/dump"; mkdir -p "$S$R/dump"
mkdir -p "$S/tmp/.X11-unix"; mountpoint -q "$S/tmp/.X11-unix" || mount --bind /tmp/.X11-unix "$S/tmp/.X11-unix"
chroot "$S" /usr/bin/env -i DISPLAY=:$D HOME=/root SDL_AUDIODRIVER=dummy \
  LD_PRELOAD=/build/glcount.so FAKECLOCK=1 KEYS="$KEYS" DUMP=$R/dump DUMPFRAMES="$FRAMES" \
  /bin/sh -c "cd $R && rm -f $CLEAN && ./$NAME $ARGS" > "$OUT/log.txt" 2>&1 &
last=${FRAMES##* }
for i in $(seq ${TIMEOUT:-1200}); do [ -f "$S$R/dump/f$last.ppm" ] && break; sleep 1; done
sleep 2
pkill -f "$R/$NAME|\./$NAME"; sleep 1; pkill -f "openbox"; pkill -f "Xvfb :$D"
umount "$S/tmp/.X11-unix"
cp "$S$R/dump"/*.ppm "$OUT/" 2>/dev/null
grep -v "^Load " "$OUT/log.txt" | head -8
echo "$(ls "$OUT"/*.ppm 2>/dev/null | wc -l) frames in $OUT"
