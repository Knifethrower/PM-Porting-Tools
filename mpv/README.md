# libmpv for PortMaster handhelds (SDL2 display context)

From the Jellyfin MPV Shim port (2026-10-05). For any port that
plays video through mpv/libmpv.

- `libmpv.so.2` (what `build_libmpv.sh` makes; not included here): mpv 0.41.0, aarch64, glibc 2.29 max, NEEDED only libc/libm/libdl/libpthread/libgcc_s,
  zlib and the firmware's `libSDL2-2.0.so.0`. FFmpeg 7.1.2 (LGPLv3, all decoders, mjpeg/png encoders,
  https via mbedTLS 3.6.4), libplacebo 7.351, libass 0.17.4 (no fontconfig: put the default font at
  `<mpv config dir>/subfont.ttf`), LuaJIT 2.1. No ALSA/Pulse (audio `ao=sdl`), no X11/Wayland/DRM.
  Shipping it: licences mpv (GPL), FFmpeg (LGPLv3), libplacebo, libass, FreeType, FriBiDi, HarfBuzz,
  LuaJIT, mbedTLS; the build files as the source offer.
- `context_sdl.c` + `gl-sdl.patch` (meson option `gl-sdl`): `--gpu-context=sdl` for `vo=gpu` and
  `vo=gpu-next`. Window + GLES 3.0/2.0 context through the firmware's SDL2 (KMSDRM, Wayland/Sway on
  ROCKNIX, Mali fbdev on Knulli), keyboard (gptokeyb) and controller from the same event loop; buttons
  arrive as mpv's positional `GAMEPAD_*` keys. Build with `-Dsdl2-gamepad=disabled` (mpv's own pad
  thread would fight the context for SDL events).
- `build_libmpv.sh`: the whole cross build (WSL: clang + lld, `~/chroot-bullseye` as sysroot, about
  25 minutes with all steps). `bash build_libmpv.sh [step...]`; output `~/jms/libmpv.so.2`.

Status: PC-tested (qemu-aarch64 + Xvfb/Mesa; the context also natively on x86_64 with a uinput pad).
Not yet run on a device; the first device run is Jellyfin MPV Shim's.

Licences: `context_sdl.c` and `gl-sdl.patch` are part of mpv, LGPL 2.1 or later (`LICENSE`); `build_libmpv.sh` is 0BSD.
