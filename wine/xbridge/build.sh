#!/bin/bash
# Builds xbridge: aarch64 for the runtime (glibc 2.28 sysroot, Westonpack's X11 libs) and x86_64 for PC tests
set -e
cd "$(dirname "$0")"
SYSROOT=${SYSROOT:-~/sysroot-buster/root}   # Debian 10 arm64 sysroot (glibc 2.28)
WLIB=${WLIB:?set WLIB=<dir>}   # lib_aarch64 of an unpacked weston_pkg_0.2 (unsquashfs)

aarch64-linux-gnu-gcc -O2 -s -Wall -march=armv8-a -mtune=cortex-a55 \
  -nostdinc -isystem /usr/lib/gcc-cross/aarch64-linux-gnu/13/include \
  -isystem $SYSROOT/usr/include/aarch64-linux-gnu -isystem $SYSROOT/usr/include -idirafter /usr/include \
  --sysroot=$SYSROOT -B$SYSROOT/usr/lib/aarch64-linux-gnu \
  -L$SYSROOT/lib/aarch64-linux-gnu -L$SYSROOT/usr/lib/aarch64-linux-gnu \
  -Wl,-rpath-link,$SYSROOT/lib/aarch64-linux-gnu -Wl,-rpath-link,$WLIB \
  -o xbridge xbridge.c $WLIB/libX11.so.6 $WLIB/libXext.so.6 $WLIB/libXfixes.so.3 $WLIB/libXtst.so.6 -ldl

gcc -O2 -Wall -o xbridge.x86_64 xbridge.c -lX11 -lXext -lXfixes -lXtst -ldl
file xbridge | cut -c1-100
aarch64-linux-gnu-objdump -T xbridge | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
aarch64-linux-gnu-readelf -d xbridge | grep NEEDED
