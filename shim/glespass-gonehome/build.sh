#!/bin/bash
# Builds libGL.so.1 (glespass) for aarch64. It has no link-time dependency on the GLES driver or
# on crusty: both are looked up at run time (dlopen), so the library also loads in processes where
# crusty is absent (westonwrap preloads it into its helper commands too).
set -e
cd "$(dirname "$0")"
# Build against an old glibc (Debian 10, 2.28) so the library loads on older firmwares too.
# SYSROOT holds the extracted libc6/libc6-dev/linux-libc-dev arm64 packages; the cross compiler's
# own (newer) headers and libraries are kept out of the search paths.
SYSROOT=${SYSROOT:-$HOME/sysroot-buster/root}
if [ -d "$SYSROOT" ]; then
  GCCINC=$(aarch64-linux-gnu-gcc -print-file-name=include)
  SRFLAGS="-nostdinc -isystem $GCCINC -isystem $SYSROOT/usr/include/aarch64-linux-gnu -isystem $SYSROOT/usr/include --sysroot=$SYSROOT -B$SYSROOT/usr/lib/aarch64-linux-gnu -L$SYSROOT/lib/aarch64-linux-gnu -L$SYSROOT/usr/lib/aarch64-linux-gnu"
else
  echo "warning: no sysroot at $SYSROOT, building against the toolchain's glibc"
fi
mkdir -p build out
python3 gen_trampolines.py
aarch64-linux-gnu-gcc $SRFLAGS -march=armv8-a -shared -fPIC -O2 -o out/libGL.so.1 glespass.c build/trampolines.S \
  -Wl,-soname,libGL.so.1 -ldl
aarch64-linux-gnu-strip out/libGL.so.1
readelf -d out/libGL.so.1 | grep -E 'NEEDED|SONAME'
echo "max GLIBC: $(readelf --dyn-syms -W out/libGL.so.1 | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1)"
echo "exported gl*: $(readelf --dyn-syms -W out/libGL.so.1 | awk '$7!="UND"{print $8}' | grep -c '^gl[A-Z]')  glX*: $(readelf --dyn-syms -W out/libGL.so.1 | awk '$7!="UND"{print $8}' | grep -c '^glX')"
readelf -r out/libGL.so.1 | grep -c R_AARCH64 || true
