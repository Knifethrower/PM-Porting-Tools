# Cross toolchain: host clang/lld -> aarch64, Debian bullseye arm64 chroot as sysroot
# (glibc 2.31, libstdc++ from GCC 10).
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=aarch64-bullseye.cmake ... (WSL, clang + lld installed on the host).
# From OpenLoco (tools-src/aarch64-bullseye.cmake).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT $ENV{HOME}/chroot-bullseye)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_C_FLAGS_INIT "-march=armv8-a")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -Wl,--as-needed")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld -Wl,--as-needed")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${CMAKE_SYSROOT})
set(ENV{PKG_CONFIG_LIBDIR} ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig:${CMAKE_SYSROOT}/opt/sdl3/lib/pkgconfig)
