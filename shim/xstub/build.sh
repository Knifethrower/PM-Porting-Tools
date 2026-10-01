#!/bin/bash
# Build the X11 stub for aarch64 (and a host build for a compile check). The X11 headers are architecture
# independent, so the host's /usr/include/X11 is used; the libraries need only libc and libpthread.
set -e
cd "$(dirname "$0")"
CF="-O2 -fPIC -shared -Wall -Wno-unused-parameter -Wno-unused-variable -idirafter /usr/include -fvisibility=default"
mkdir -p out
aarch64-linux-gnu-gcc $CF -Wl,-soname,libX11.so.6 -o out/libX11.so.6 xstub.c -lpthread
aarch64-linux-gnu-gcc $CF -Wl,-soname,libXcursor.so.1 -o out/libXcursor.so.1 xcursor_stub.c -L out -l:libX11.so.6
aarch64-linux-gnu-gcc $CF -Wl,-soname,libglxsdl.so -o out/libglxsdl.so glxsdl.c -ldl
aarch64-linux-gnu-strip out/libX11.so.6 out/libXcursor.so.1 out/libglxsdl.so
gcc $CF -o out/libX11-host.so xstub.c -lpthread
ls -l out
