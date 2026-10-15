> **Updated 2026-10-05** to the sources shipped with the Ittle Dew and Teslagrad ports: `xstub.c` (libX11 stand-in, `XSTUB_SCREEN=WxH`), `stubs.c` (libXcursor + libGLU stand-ins), `glxsdl.c` (GLX on SDL2: main-FBO 16:9 letterbox, loading screen, ASTC relabel, joystick filter; settings from the file named by `GLXSDL_CONFIG`: `dxt5_astc = 0|1|2`, `pad = <joystick name>`), `astc_dec.cpp` (ASTC decode fallback for GPUs without ASTC; needs ARM's astc-encoder), `build.sh`. `glxsdl-offscreen.patch` applies to the pre-letterbox glxsdl only (superseded); `xstubwrap.sh` / `xtracewrap.sh` are Westonpack-era debugging helpers.

# xstub: libX11 stand-in for Unity 4 players under box64 + Westonpack

A native aarch64 `libX11.so.6` (plus a tiny `libXcursor.so.1`) that answers a Unity 4 Linux player's Xlib calls
locally, so no X server is needed. Built and measured on Ittle Dew (Sep 2026): with westonwrap unchanged and
`WRAPPED_LIBRARY_PATH=<dir with the stub>` on the app's env line, Weston never spawns Xwayland (no X client
connects): -17 MB free memory on an RG Cube XX (Knulli), -15..19 MB on an RG351P (ROCKNIX), same frame rate.
`xstubwrap.sh` runs the game with no compositor at all: on ROCKNIX crusty presents to Sway; on Knulli it uses the
system SDL2's `mali` video driver (Mali fbdev EGL on /dev/fb0, the same path EmulationStation and the native GLES
ports use; Knulli's SDL2 has only `mali` and `dummy`, no KMSDRM). Cube XX with that: only the game process, 60 fps
flat, +21..34 MB free in the world, input from evdev.
Not shipped with Ittle Dew (too little gain for a 900-line replacement of a tested library); kept for a port
where Weston + Xwayland is the difference between fitting and not fitting.

What the player needs (learned the hard way on Ittle Dew):
- box64's libX11 wrapper dereferences `_XLockMutex_fn` / `_XUnlockMutex_fn` at load and reads `struct _XDisplay`
  fields: keep the real Xlib struct layouts.
- crusty initialises gl4es only with `CRUSTY_GL4ES=1` (westonwrap sets it; without it the first glClear crashes).
- an EWMH window-manager face (`_NET_SUPPORTING_WM_CHECK`, `_NET_SUPPORTED`, `_NET_WM_STATE` handling), the event
  order Xwayland + its WM send on map (Configure, Map, Visibility, Configure, FocusIn), ConfigureNotify on
  XRaiseWindow, XGetInputFocus = PointerRoot until mapped.
- keyboard from evdev with raw syscalls (crusty/gl4es hook libc file functions in the process); glibc's
  `syscall()` returns -1 + errno; virtual keyboards come and go under the same node name (liveness check);
  opening evdev nodes costs 70-135 ms on the H700 kernel, so rejected nodes are not reopened every scan (it showed
  as a 60/48 fps alternation).
- `xtracewrap.sh` + Westonpack's `tools/xtrace` records the real X traffic of a normal run (run the proxy outside
  the crusty preload).
Build: `build.sh` inside the Debian bullseye arm64 chroot (`../../build/native-aarch64/chroot.sh`; needs
libx11-dev, libxcursor-dev, libgl-dev and astc-encoder 5.3.0 at ~/astc-encoder in the chroot). glibc, libstdc++
and libgcc stay dynamic: PortMaster forbids statically linking core system libraries. On an x86_64 host the same
script only compile-checks the stub. `XSTUB_LOG=1` logs every call.
