#!/bin/bash
# Simulates a PortMaster device for a native aarch64 port's launcher, on the PC (WSL):
# a fake control folder (control.txt, PortMaster's libgl_default.txt, 7zzs), the zip unpacked under
# a fake roms tree with the permissions PortMaster's installer leaves, and the port binary wrapped:
# the wrapper logs the environment the launcher set up, then runs the real aarch64 binary through
# qemu-user against an arm64 sysroot on Xvfb. Shows log.txt and the port folder afterwards.
#
# usage: pm_sim_test.sh <port.zip> [runs]
#   SYSROOT  arm64 sysroot / chroot with the game's libraries (default ~/chroot-bullseye)
#   BIN      binary in the port folder to wrap (default: the only *.aarch64 there)
#   TIMEOUT  seconds the game runs per launch (default 25)
#   DUMMY=1  SDL dummy video instead of Xvfb (games that need no GL)
#   XDISP    Xvfb display number (default 97)
#   runs     launch the port this many times (first-launch setup, then normal start; default 1)
# Needs qemu-user-static, Xvfb, unzip; 7z for ports that unpack archives with 7zzs.
# Started in Torus Trooper/Cube/Mu-cade/Hocus Pocus; for a launcher that runs a patcher setup
# script see ../pc-testing/fake-patcher.
set -e
ZIP=$(realpath "$1"); RUNS=${2:-1}
SYSROOT="${SYSROOT:-$HOME/chroot-bullseye}"
D=${XDISP:-97}
T=/tmp/pmsim_$(basename "$ZIP" .zip)
[ -f "$ZIP" ] || { echo "usage: pm_sim_test.sh <port.zip> [runs]"; exit 1; }
rm -rf "$T"; mkdir -p "$T/roms/ports" "$T/xdg/PortMaster"
(cd "$T/roms/ports" && unzip -q "$ZIP")
command -v 7z >/dev/null && ln -s "$(command -v 7z)" "$T/xdg/PortMaster/7zzs.aarch64"
cat > "$T/xdg/PortMaster/control.txt" <<CTL
directory=${T#/}/roms
DEVICE_ARCH=aarch64
CFW_NAME=TestOS
ESUDO=""
GPTOKEYB="echo gptokeyb"
GPTOKEYB2="echo gptokeyb2"
sdl_controllerconfig="test-config"
get_controls() { :; }
pm_platform_helper() { echo "pm_platform_helper \$*"; }
pm_finish() { echo pm_finish; }
CTL
cat > "$T/xdg/PortMaster/libgl_default.txt" <<'LGL'
export LIBGL_ES=2
export LIBGL_GL=21
export LIBGL_FB=4
if [ ! -e "/dev/dri/card0" ]; then
  export LIBGL_FB=2
fi
if [ -d "$PWD/gl4es.$DEVICE_ARCH" ]; then
  export LD_LIBRARY_PATH="$PWD/gl4es.$DEVICE_ARCH:$LD_LIBRARY_PATH"
elif [ -d "$PWD/gl4es" ]; then
  export LD_LIBRARY_PATH="$PWD/gl4es:$LD_LIBRARY_PATH"
fi
if [ -d "$PWD/libs.$DEVICE_ARCH" ]; then
  export LD_LIBRARY_PATH="$PWD/libs.$DEVICE_ARCH:$LD_LIBRARY_PATH"
elif [ -d "$PWD/libs" ]; then
  export LD_LIBRARY_PATH="$PWD/libs:$LD_LIBRARY_PATH"
fi
LGL
LAUNCHER=$(cd "$T/roms/ports" && ls *.sh | head -1)
G=$(find "$T/roms/ports" -mindepth 1 -maxdepth 1 -type d | head -1)
B=${BIN:-$(cd "$G" && ls *.aarch64 | head -1)}
mv "$G/$B" "$G/$B.real"
if [ "$DUMMY" = 1 ]; then VIDEO="export SDL_VIDEODRIVER=dummy"; else VIDEO="export DISPLAY=:$D"; fi
cat > "$G/$B" <<WRAP
#!/bin/bash
echo "cwd=\$PWD args=\$*"
env | grep -E "^(LIBGL_|SDL_|LD_LIBRARY_PATH|LD_PRELOAD)" | sort
unset LD_LIBRARY_PATH SDL_VIDEO_GL_DRIVER SDL_VIDEO_EGL_DRIVER
$VIDEO
export SDL_AUDIODRIVER=dummy
timeout ${TIMEOUT:-25} qemu-aarch64-static -L "$SYSROOT" "$G/$B.real" "\$@"
echo "game exit: \$?"
WRAP
# As PortMaster's installer leaves a port: harbourmaster runs chmod -R 777 over it on ext
# filesystems (_fix_permissions), and exFAT/FAT cards have no permission bits at all.
chmod -R 777 "$T/roms/ports"
if [ "$DUMMY" != 1 ]; then
  pkill -f "Xvfb :$D" || true
  Xvfb :$D -screen 0 ${RES:-640x480}x24 -nolisten tcp >/dev/null 2>&1 &
  sleep 1
fi
for r in $(seq "$RUNS"); do
  echo "=== run $r: $LAUNCHER"
  XDG_DATA_HOME="$T/xdg" bash "$T/roms/ports/$LAUNCHER" > "$T/launcher_out_$r.txt" 2>&1 || true
  echo "--- log.txt"; grep -v "^Load " "$G/log.txt" 2>/dev/null | tail -40
done
[ "$DUMMY" = 1 ] || pkill -f "Xvfb :$D" || true
echo "=== port folder"; ls "$G"
