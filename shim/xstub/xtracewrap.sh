#!/bin/bash
# TEST ONLY. xtracewrap.sh [VAR=value ...] <command...>: like westonwrap's crusty_glx_gl4es mode, but with an
# xtrace proxy between the game and Xwayland (/tmp/xtrace.log) so the real X11 traffic can be read. Westonpack
# is started without a command (it stays up), the proxy runs without crusty's preload, the game gets DISPLAY=:9.
weston_dir=${weston_dir:-/tmp/weston}
"$weston_dir/westonwrap.sh" headless noop kiosk crusty_glx_gl4es
xsock=:0; [ -e /tmp/.X11-unix/X1 ] && xsock=:1
LD_LIBRARY_PATH="$weston_dir/lib_aarch64" env -u LD_PRELOAD "$weston_dir/tools/xtrace" -n -d $xsock -D :9 -o /tmp/xtrace.log -k >/tmp/xtrace.err 2>&1 &
for i in $(seq 50); do [ -e /tmp/.X11-unix/X9 ] && break; sleep 0.1; done
echo "xtracewrap: sockets: $(ls /tmp/.X11-unix | tr '
' ' ')"
findlib="$weston_dir/tools/findlib"
export CRUSTY_LIBEGL=${CRUSTY_LIBEGL:-$($findlib libEGL.so)}
for lib in libSDL2.so libSDL2.so.0 libSDL2-2.0.so libSDL2-2.0.so.0; do
  out="$($findlib "$lib" 2>/dev/null)" && [ -n "$out" ] && { export CRUSTY_LIBSDL="$(echo "$out" | tail -n 1)"; break; }
done
export CRUSTY_BLOCK_INPUT=1 CRUSTY_SHOW_CURSOR=0 CRUSTY_GL4ES=1 XDG_SESSION_TYPE=x11 DISPLAY=:9
export CRUSTY_WLMODE=1 CRUSTY_WLDISP=$WAYLAND_DISPLAY WAYLAND_DISPLAY=
unset SDL_VIDEODRIVER
gllib="$weston_dir/lib_aarch64/graphics/gl4es_glxpass:$weston_dir/lib_aarch64/graphics/crusty_glx"
export LD_LIBRARY_PATH="$gllib:$weston_dir/lib_aarch64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_PRELOAD="${WRAPPED_PRELOAD_MALI:+$WRAPPED_PRELOAD_MALI:}$weston_dir/lib_aarch64/graphics/crusty_glx/libcrusty.so"
echo "xtracewrap: X on $xsock, proxy :9, xtrace pid $!"
env "$@"
pkill -f "tools/xtrace" 2>/dev/null
