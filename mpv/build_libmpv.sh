#!/bin/bash
# Cross-builds libmpv.so.2 for aarch64 PortMaster devices (glibc 2.31, Debian bullseye sysroot).
# Everything is static inside the one .so except what every firmware has: SDL2 (video, audio, pad), zlib, libc.
# Host clang + lld with the bullseye chroot as the only sysroot: Ubuntu's aarch64-linux-gnu-gcc puts its own
# glibc 2.39 headers and libraries ahead of any --sysroot (the result needed GLIBC_2.38).
# Run in WSL: bash build_libmpv.sh [step...]   (steps: mbedtls ffmpeg luajit freetype fribidi harfbuzz libass libplacebo mpv)
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
SYSROOT=${SYSROOT:-$HOME/chroot-bullseye}
TOP=${TOP:-$HOME/jms}
SRC=$TOP/src
BLD=$TOP/build
PREFIX=$TOP/prefix
JOBS=$(nproc)

MBEDTLS=3.6.4
FFMPEG=7.1.2
FREETYPE=2.13.3
FRIBIDI=1.0.16
HARFBUZZ=11.2.1
LIBASS=0.17.4
LIBPLACEBO=v7.351.0
MPV=v0.41.0

TARGET="--target=aarch64-linux-gnu --sysroot=$SYSROOT"
CC_T="clang $TARGET"
CXX_T="clang++ $TARGET"
CFLAGS_T="-O2 -fPIC -mcpu=cortex-a53"
LDFLAGS_T="-fuse-ld=lld"
BIN=aarch64-linux-gnu-
mkdir -p "$SRC" "$BLD" "$PREFIX/lib/pkgconfig"
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig
export PKG_CONFIG_PATH=

fetch() {   # url dir
    [ -d "$SRC/$2" ] && return
    mkdir -p "$SRC/$2"
    curl -sSfL -o "$SRC/$2.tar" "$1"
    tar -xf "$SRC/$2.tar" -C "$SRC/$2" --strip-components=1 && rm "$SRC/$2.tar"
}

# .pc files for the firmware libraries, with the sysroot paths written in
sysroot_pc() {
    local d=$SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig
    for f in sdl2 zlib; do
        sed -e "s|^prefix=/usr|prefix=$SYSROOT/usr|" -e "s|^libdir=.*|libdir=$SYSROOT/usr/lib/aarch64-linux-gnu|" \
            "$d/$f.pc" > "$PREFIX/lib/pkgconfig/$f.pc"
    done
}

list() { printf "'%s'," "$@"; }

meson_cross() {
    cat > "$BLD/cross.txt" <<EOF
[binaries]
c = [$(list $CC_T)]
cpp = [$(list $CXX_T)]
ar = '${BIN}ar'
strip = '${BIN}strip'
pkg-config = 'pkg-config'
[built-in options]
c_args = [$(list $CFLAGS_T)]
cpp_args = [$(list $CFLAGS_T)]
c_link_args = [$(list $LDFLAGS_T)]
cpp_link_args = [$(list $LDFLAGS_T)]
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'cortex-a53'
endian = 'little'
EOF
}

meson_build() {   # name srcdir options...
    local name=$1 dir=$2; shift 2
    rm -rf "$BLD/$name"
    meson setup "$BLD/$name" "$dir" --cross-file "$BLD/cross.txt" --prefix "$PREFIX" --libdir lib \
        --buildtype release --default-library "${LIBTYPE:-static}" "$@"
    ninja -C "$BLD/$name" install
}

step_mbedtls() {
    fetch "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS/mbedtls-$MBEDTLS.tar.bz2" mbedtls
    rm -rf "$BLD/mbedtls"
    cmake -S "$SRC/mbedtls" -B "$BLD/mbedtls" -G Ninja -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
        -DCMAKE_C_COMPILER=clang -DCMAKE_C_COMPILER_TARGET=aarch64-linux-gnu -DCMAKE_SYSROOT="$SYSROOT" \
        -DCMAKE_C_FLAGS="$CFLAGS_T" -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_MAKE_PROGRAM="$(command -v ninja)" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
    ninja -C "$BLD/mbedtls" install
}

step_ffmpeg() {
    fetch "https://ffmpeg.org/releases/ffmpeg-$FFMPEG.tar.xz" ffmpeg
    rm -rf "$BLD/ffmpeg" && mkdir -p "$BLD/ffmpeg" && cd "$BLD/ffmpeg"
    "$SRC/ffmpeg/configure" --prefix="$PREFIX" --enable-cross-compile --arch=aarch64 --cpu=cortex-a53 \
        --target-os=linux --cc="$CC_T" --cxx="$CXX_T" --cross-prefix=$BIN --pkg-config=pkg-config \
        --extra-cflags="-fPIC -I$PREFIX/include" --extra-ldflags="$LDFLAGS_T -L$PREFIX/lib" \
        --enable-pic --enable-static --disable-shared --disable-programs --disable-doc --disable-debug \
        --disable-autodetect --enable-zlib --enable-iconv --enable-version3 --enable-mbedtls \
        --disable-encoders --enable-encoder=mjpeg,png --disable-avdevice --disable-postproc \
        --disable-hwaccels --disable-devices
    make -j"$JOBS" install
}

step_luajit() {
    [ -d "$SRC/luajit" ] || git clone -q https://github.com/LuaJIT/LuaJIT.git "$SRC/luajit"
    cd "$SRC/luajit" && make clean >/dev/null
    make -j"$JOBS" HOST_CC=gcc CROSS= STATIC_CC="$CC_T" DYNAMIC_CC="$CC_T -fPIC" TARGET_LD="$CC_T" \
        TARGET_AR="${BIN}ar rcus" TARGET_STRIP=${BIN}strip TARGET_SYS=Linux BUILDMODE=static \
        TARGET_CFLAGS="$CFLAGS_T" TARGET_LDFLAGS="$LDFLAGS_T" amalg
    make install PREFIX="$PREFIX"
    rm -f "$PREFIX"/lib/libluajit-5.1.so*
}

step_freetype() {
    fetch "https://download.savannah.gnu.org/releases/freetype/freetype-$FREETYPE.tar.xz" freetype
    meson_build freetype "$SRC/freetype" -Dbrotli=disabled -Dbzip2=disabled -Dharfbuzz=disabled \
        -Dpng=disabled -Dzlib=disabled -Dtests=disabled
}

step_fribidi() {
    fetch "https://github.com/fribidi/fribidi/releases/download/v$FRIBIDI/fribidi-$FRIBIDI.tar.xz" fribidi
    meson_build fribidi "$SRC/fribidi" -Ddocs=false -Dbin=false -Dtests=false
}

step_harfbuzz() {
    fetch "https://github.com/harfbuzz/harfbuzz/releases/download/$HARFBUZZ/harfbuzz-$HARFBUZZ.tar.xz" harfbuzz
    meson_build harfbuzz "$SRC/harfbuzz" -Dfreetype=enabled -Dglib=disabled -Dgobject=disabled -Dcairo=disabled \
        -Dicu=disabled -Dtests=disabled -Ddocs=disabled -Dutilities=disabled -Dintrospection=disabled
}

step_libass() {
    fetch "https://github.com/libass/libass/releases/download/$LIBASS/libass-$LIBASS.tar.xz" libass
    rm -rf "$BLD/libass" && mkdir -p "$BLD/libass" && cd "$BLD/libass"
    CC="$CC_T" CFLAGS="$CFLAGS_T" LDFLAGS="$LDFLAGS_T" \
        "$SRC/libass/configure" --host=aarch64-linux-gnu --prefix="$PREFIX" --enable-static --disable-shared \
        --disable-fontconfig --disable-require-system-font-provider --disable-libunibreak --disable-asm
    make -j"$JOBS" install
}

step_libplacebo() {
    [ -d "$SRC/libplacebo" ] || git clone -q --recursive --depth 1 -b $LIBPLACEBO \
        https://code.videolan.org/videolan/libplacebo.git "$SRC/libplacebo"
    meson_build libplacebo "$SRC/libplacebo" -Dvulkan=disabled -Dopengl=enabled -Dd3d11=disabled \
        -Dglslang=disabled -Dshaderc=disabled -Dlcms=disabled -Ddovi=disabled -Dlibdovi=disabled \
        -Ddemos=false -Dtests=false -Dbench=false -Dfuzz=false -Dxxhash=disabled -Dunwind=disabled
}

step_mpv() {
    [ -d "$SRC/mpv" ] || git clone -q --depth 1 -b $MPV https://github.com/mpv-player/mpv.git "$SRC/mpv"
    cd "$SRC/mpv"
    git checkout -q -- . && git clean -qfd
    git apply "$HERE/mpv/gl-sdl.patch"
    cp "$HERE/mpv/context_sdl.c" video/out/opengl/
    LIBTYPE=shared meson_build mpv "$SRC/mpv" -Dlibmpv=true -Dcplayer=false -Dtests=false -Dfuzzers=false \
        -Dauto_features=disabled -Dgl=enabled -Dgl-sdl=enabled -Dplain-gl=enabled -Dlua=luajit \
        -Dlibavdevice=disabled -Dzlib=enabled -Diconv=enabled -Dsdl2-audio=enabled \
        -Dsdl2-gamepad=disabled -Dsdl2-video=disabled \
        -Dc_link_args="$LDFLAGS_T -Wl,--exclude-libs,ALL -Wl,-Bsymbolic"
    local so=$(readlink -f "$PREFIX/lib/libmpv.so.2")
    ${BIN}strip --strip-unneeded -o "$TOP/libmpv.so.2" "$so"
    ${BIN}readelf -d "$TOP/libmpv.so.2" | grep NEEDED
    ${BIN}objdump -T "$TOP/libmpv.so.2" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
    ls -la "$TOP/libmpv.so.2"
}

meson_cross
sysroot_pc
steps=${*:-mbedtls ffmpeg luajit freetype fribidi harfbuzz libass libplacebo mpv}
for s in $steps; do
    echo "=== $s"
    ( step_$s )
done
