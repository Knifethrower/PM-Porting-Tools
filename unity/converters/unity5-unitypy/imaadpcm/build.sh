#!/bin/bash
# Builds the IMA ADPCM encoder used by tools/audio.py (run in WSL / Linux).
# The library uses no libc functions, so it loads on any glibc.
set -e
cd "$(dirname "$0")"
CFLAGS="-O2 -shared -fPIC -fvisibility=hidden -nostdlib -ffreestanding"
aarch64-linux-gnu-gcc $CFLAGS -march=armv8-a -mtune=cortex-a55 -o libimaadpcm.aarch64.so imaadpcm.c
gcc $CFLAGS -o libimaadpcm.x86_64.so imaadpcm.c
# Windows DLL, only for testing the installer on a PC
x86_64-w64-mingw32-gcc -O2 -shared -o imaadpcm.dll imaadpcm.c
aarch64-linux-gnu-strip libimaadpcm.aarch64.so; strip libimaadpcm.x86_64.so
ls -la
