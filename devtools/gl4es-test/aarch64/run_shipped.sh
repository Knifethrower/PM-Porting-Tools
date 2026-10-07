#!/bin/bash
# Runs gt tests against the exact aarch64 gl4es a port ships, under qemu-aarch64 with the bullseye arm64 chroot's
# Mesa (llvmpipe GLES 2) as its backend, on the PC's Xvfb. Proves a finding is in the shipped bits, not only in
# the x86_64 rebuild. Slow (qemu): use it for crash / program-rejection checks on chosen tests.
#   run_shipped.sh <port's gamedir> <outdir> [gt args: -c CORPUS / test names / -m SUBSTR]
# Same stack as the Unity 4 launchers: <gamedir>/gl4es.aarch64/libGL.so.1 (Westonpack's glxpass gl4es, patched)
# + <gamedir>/libs.aarch64/libglxsdl.so (its crusty_glX* on SDL2) preloaded, the launcher's gl4es settings.
# gl4es runs its real hardware test here (no LIBGL_NOTEST: the shipped build has no Mali profile).
HERE=$(cd "$(dirname "$0")/.." && pwd); . "$HERE/env.sh"
G=$1; OUT=$2; shift 2
C=${CHROOT:-$HOME/chroot-bullseye}
B=$GT/bin/gt.aarch64
if [ ! -x "$B" ] || [ "$HERE/gt/main.c" -nt "$B" ]; then
    clang --target=aarch64-linux-gnu --sysroot="$C" -fuse-ld=lld -O1 -g -w -o "$B" "$HERE"/gt/*.c -lGL -lX11 -lm || exit 1
fi
# LD_PRELOAD splits on spaces ("Ittle Dew"): run from copies
P=$GT/aarch64-port; mkdir -p "$P"
cp "$G/gl4es.aarch64/libGL.so.1" "$P/libGL.so.1"; cp "$G/libs.aarch64/libglxsdl.so" "$P/"   # real Xlib here: SDL needs it
sha1sum "$P/libGL.so.1" | cut -c1-8 > "$OUT.sha1" 2>/dev/null
G=$P
start_xvfb 82 || exit 1
mkdir -p "$OUT"
LD_LIBRARY_PATH="$C/usr/lib/aarch64-linux-gnu:$C/lib/aarch64-linux-gnu" LIBGL_DRIVERS_PATH="$C/usr/lib/aarch64-linux-gnu/dri" \
  LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=1 SDL_VIDEODRIVER=x11 GT_NOWINDOW=1 \
  LIBGL_NOBANNER=1 LIBGL_SILENTSTUB=1 LIBGL_AVOID16BITS=0 LIBGL_NOBGRA=1 \
  qemu-aarch64 -L "$C" -E LD_PRELOAD="$G/libGL.so.1:$G/libglxsdl.so" \
  "$B" -o "$OUT" "$@" > "$OUT/stdout.txt" 2> "$OUT/stderr.txt"
echo "exit $?"
kill $XVFB_PID
