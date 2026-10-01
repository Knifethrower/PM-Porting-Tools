#!/bin/bash
# Builds box64 0.4.4 for the Wine runtime: the portable build (Shared/box64 box64-portable: glibc 2.28
# sysroot, -march=armv8-a -mtune=cortex-a55, BAD_SIGNAL) plus wine-reserve.patch. Run in WSL.
set -euo pipefail
SRC=~/box64build/box64-0.4.4
WSRC=~/box64build/box64-0.4.4-wine
SR=~/sysroot-buster/root
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$HERE/box64

if [ ! -d "$WSRC" ]; then
  rsync -a --exclude '/build' --exclude '/build-*' "$SRC/" "$WSRC/"
  patch -d "$WSRC" -p1 < "$HERE/wine-reserve.patch"
fi
mkdir -p "$WSRC/build" && cd "$WSRC/build"
cmake .. -DARM64=1 -DARM_DYNAREC=ON -DBAD_SIGNAL=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_ASM_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_C_FLAGS="-nostdinc -isystem /usr/lib/gcc-cross/aarch64-linux-gnu/13/include -isystem $SR/usr/include/aarch64-linux-gnu -isystem $SR/usr/include --sysroot=$SR -march=armv8-a -mtune=cortex-a55" \
  -DCMAKE_EXE_LINKER_FLAGS="--sysroot=$SR -B$SR/usr/lib/aarch64-linux-gnu -L$SR/lib/aarch64-linux-gnu -L$SR/usr/lib/aarch64-linux-gnu -Wl,-rpath-link,$SR/lib/aarch64-linux-gnu" \
  > cmake.log
make -j"$(nproc)" box64 > make.log 2>&1 || { tail -30 make.log; exit 1; }
aarch64-linux-gnu-strip -o "$OUT" box64
ls -la "$OUT"
aarch64-linux-gnu-objdump -T "$OUT" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
