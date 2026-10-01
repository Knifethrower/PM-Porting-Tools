#!/bin/bash
# xstubwrap.sh [VAR=value ...] <command...>: run an X11 game on Westonpack's crusty + gl4es without Weston or
# Xwayland. The X11 stub (libX11.so.6 / libXcursor.so.1 next to this script) answers the game's Xlib calls and
# reads the keyboard from evdev; crusty presents through SDL2 as usual. The Westonpack squashfs must be mounted at
# $weston_dir (the launcher does that). Mirrors westonwrap.sh's "crusty_glx_gl4es" setup, minus the compositor.
weston_dir=${weston_dir:-/tmp/weston}
stub_dir="$(cd "$(dirname "$0")" && pwd -P)"
findlib="$weston_dir/tools/findlib"
export CRUSTY_LIBEGL=${CRUSTY_LIBEGL:-$($findlib libEGL.so)}
if [ -z "$CRUSTY_LIBSDL" ]; then
  for lib in libSDL2.so libSDL2.so.0 libSDL2-2.0.so libSDL2-2.0.so.0; do
    out="$($findlib "$lib" 2>/dev/null)" && [ -n "$out" ] && { export CRUSTY_LIBSDL="$(echo "$out" | tail -n 1)"; break; }
  done
fi
export CRUSTY_LIBGBM=${CRUSTY_LIBGBM:-$($findlib libgbm.so.1)} CRUSTY_LIBDRM=${CRUSTY_LIBDRM:-$($findlib libdrm.so)}
export CRUSTY_BLOCK_INPUT=1 CRUSTY_SHOW_CURSOR=0 CRUSTY_GL4ES=1 XDG_SESSION_TYPE=x11 DISPLAY=:0   # as westonwrap's crusty_glx_gl4es mode
if [ "$CFW_NAME" = "ROCKNIX" ]; then          # crusty presents to Sway's Wayland display, as westonwrap does there
  export CRUSTY_WLMODE=1 CRUSTY_WLDISP=$WAYLAND_DISPLAY WAYLAND_DISPLAY=
  unset SDL_VIDEODRIVER
else                                          # no compositor at all: SDL2 goes straight to KMS/DRM
  export CRUSTY_WLMODE=0 SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-mali}       # Knulli SDL2: "mali" = Mali fbdev EGL driver
fi
gllib="$weston_dir/lib_aarch64/graphics/gl4es_glxpass:$weston_dir/lib_aarch64/graphics/crusty_glx"
export LD_LIBRARY_PATH="$stub_dir:$gllib:$weston_dir/lib_aarch64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_PRELOAD="${WRAPPED_PRELOAD_MALI:+$WRAPPED_PRELOAD_MALI:}$weston_dir/lib_aarch64/graphics/crusty_glx/libcrusty.so${LD_PRELOAD:+:$LD_PRELOAD}"
echo "xstubwrap: EGL=$CRUSTY_LIBEGL SDL=$CRUSTY_LIBSDL WLMODE=$CRUSTY_WLMODE"
exec env "$@"
