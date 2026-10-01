#!/bin/bash
# Builds glxcheck: aarch64 (glibc 2.28 sysroot, Westonpack's libX11) and x86_64 for PC tests
set -e
cd "$(dirname "$0")"
SYSROOT=${SYSROOT:-~/sysroot-buster/root}   # Debian 10 arm64 sysroot (glibc 2.28)
WLIB=${WLIB:-/mnt/c/Claude/NITW/port/runtimes/weston/lib_aarch64}   # lib_aarch64 of an unpacked weston_pkg_0.2 (unsquashfs)
aarch64-linux-gnu-gcc -O2 -s -Wall -march=armv8-a \
  -nostdinc -isystem /usr/lib/gcc-cross/aarch64-linux-gnu/13/include \
  -isystem $SYSROOT/usr/include/aarch64-linux-gnu -isystem $SYSROOT/usr/include -idirafter /usr/include \
  --sysroot=$SYSROOT -B$SYSROOT/usr/lib/aarch64-linux-gnu \
  -L$SYSROOT/lib/aarch64-linux-gnu -L$SYSROOT/usr/lib/aarch64-linux-gnu \
  -Wl,-rpath-link,$SYSROOT/lib/aarch64-linux-gnu -Wl,-rpath-link,$WLIB \
  -o glxcheck glxcheck.c $WLIB/libX11.so.6 -ldl
gcc -O2 -Wall -o glxcheck.x86_64 glxcheck.c -lX11 -ldl
file glxcheck | cut -c1-80
