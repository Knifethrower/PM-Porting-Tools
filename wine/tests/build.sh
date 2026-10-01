#!/bin/bash
# Builds the 32-bit test programs into ./out (Wine Test port: release/winetest/test) (WSL, gcc-mingw-w64-i686)
set -e
cd "$(dirname "$0")"
OUT=${OUT:-out}; mkdir -p $OUT
i686-w64-mingw32-gcc -O2 -s -mwindows -o $OUT/1-gditest.exe gditest.c -lgdi32
i686-w64-mingw32-gcc -O2 -s -mwindows -o $OUT/2-ddrawtest.exe ddrawtest.c -lddraw -lgdi32
i686-w64-mingw32-gcc -O2 -s -mwindows -o $OUT/3-d3d9test.exe d3d9test.c -ld3d9
ls -la $OUT
