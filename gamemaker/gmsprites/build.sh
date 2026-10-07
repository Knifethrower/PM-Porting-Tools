#!/bin/bash
# Build gmsprites in WSL: gmsprites.aarch64 (host clang/lld, Debian bullseye arm64 chroot
# ~/chroot-bullseye as sysroot: glibc <= 2.29, dynamic libstdc++) and gmsprites.x86_64 (host gcc,
# PC tests). Needs ARM's astc-encoder source in ~/astc-encoder (5.3.0 here).
#   wsl -e bash build.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
A=$HOME/astc-encoder
TC=$HERE/../../build/native-aarch64/aarch64-bullseye.cmake
B=$HOME/gmsprites-build
mkdir -p "$B"

# aarch64
cmake -S "$A" -B "$B/astc-a64" -DCMAKE_TOOLCHAIN_FILE="$TC" -DCMAKE_BUILD_TYPE=Release \
      -DASTCENC_ISA_NEON=ON -DASTCENC_CLI=OFF -DASTCENC_SHAREDLIB=OFF > "$B/cmake-a64.log"
cmake --build "$B/astc-a64" -j8 > "$B/make-a64.log"
CC="clang --target=aarch64-linux-gnu --sysroot=$HOME/chroot-bullseye -march=armv8-a -O2"
$CC -Wall -c "$HERE/gmsprites.c" -o "$B/gmsprites-a64.o"
${CC/clang/clang++} -Wall -c "$HERE/astc_shim.cpp" -I"$A/Source" -o "$B/shim-a64.o"
${CC/clang/clang++} -fuse-ld=lld -Wl,--as-needed "$B/gmsprites-a64.o" "$B/shim-a64.o" \
    "$B/astc-a64/Source/libastcenc-neon-static.a" -lz -lpthread -o "$HERE/gmsprites.aarch64"
llvm-strip "$HERE/gmsprites.aarch64"

# x86_64 (PC)
cmake -S "$A" -B "$B/astc-x64" -DCMAKE_BUILD_TYPE=Release -DASTCENC_ISA_AVX2=ON -DASTCENC_CLI=OFF \
      -DASTCENC_SHAREDLIB=OFF > "$B/cmake-x64.log"
cmake --build "$B/astc-x64" -j8 > "$B/make-x64.log" 2>&1
gcc -O2 -Wall -c "$HERE/gmsprites.c" -o "$B/gmsprites-x64.o"
g++ -O2 -Wall -c "$HERE/astc_shim.cpp" -I"$A/Source" -o "$B/shim-x64.o"
g++ "$B/gmsprites-x64.o" "$B/shim-x64.o" "$B/astc-x64/Source/libastcenc-avx2-static.a" -lz -lpthread \
    -o "$HERE/gmsprites.x86_64"
strip "$HERE/gmsprites.x86_64"
file "$HERE/gmsprites.aarch64" "$HERE/gmsprites.x86_64"
