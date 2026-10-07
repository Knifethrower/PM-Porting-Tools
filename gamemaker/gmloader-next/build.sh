#!/bin/bash
# Build gmloader-next for armhf with some or all of the patches in this folder, in the Debian
# bullseye amd64 chroot with the armhf cross toolchain (upstream CI's setup, glibc 2.31).
# Run as root in WSL:   wsl -u root -e bash build.sh [01 02 03 04]     (default: all)
# Output: ~/gmloadernext-out/gmloadernext.armhf
# Setup (once): git clone --recursive https://github.com/JohnnyonFlame/gmloader-next ~/gmloader-next
#   (522d964); chroot ~/chroot-bullseye-amd64 with: dpkg --add-architecture armhf; apt-get install
#   crossbuild-essential-armhf libsdl2-dev:armhf libzip-dev:armhf libopenal-dev:armhf zlib1g-dev:armhf
#   python3 python3-clang-11 libclang1-11 libclang-11-dev pkg-config
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
U=/home/$(ls /home | head -1)
R=$U/chroot-bullseye-amd64
PATCHES=${*:-01 02 03 04}
for m in proc sys dev dev/pts; do mountpoint -q "$R/$m" || mount --bind "/$m" "$R/$m"; done
rm -rf "$R/work/gmloader-next"
mkdir -p "$R/work"
cp -a "$U/gmloader-next" "$R/work/"
OPENAL=0
for p in $PATCHES; do
    patch -d "$R/work/gmloader-next" -p1 -s < "$HERE"/$p-*.patch
    case $p in
        02) OPENAL=1 ;;
        03) cp "$HERE/letterbox.cpp" "$R/work/gmloader-next/gmloader/" ;;
        04) cp "$HERE/spritehack.cpp" "$R/work/gmloader-next/gmloader/" ;;
    esac
done
cat > "$R/work/make.sh" << 'EOF'
cd /work/gmloader-next
m() { make -f Makefile.gmloader ARCH=arm-linux-gnueabihf USE_OPENAL_THUNKS=$OPENAL \
      LLVM_FILE=/usr/lib/llvm-11/lib/libclang-11.so LLVM_INC=/usr/arm-linux-gnueabihf/include/c++/10/arm-linux-gnueabihf "$@"; }
# -j8 can race libzip's generated zipconf.h; the second run finishes serially
m -j8 > build.log 2>&1 || m -j1 >> build.log 2>&1 || { grep -E 'error:|undefined reference' build.log | head -20; exit 1; }
arm-linux-gnueabihf-strip -o /work/gmloadernext.armhf build/arm-linux-gnueabihf/gmloader/gmloadernext.armhf
EOF
chroot "$R" /usr/bin/env OPENAL=$OPENAL /bin/bash /work/make.sh
mkdir -p "$U/gmloadernext-out"
cp "$R/work/gmloadernext.armhf" "$U/gmloadernext-out/gmloadernext.armhf"
echo "patches: $PATCHES"
ls -la "$U/gmloadernext-out/gmloadernext.armhf"
