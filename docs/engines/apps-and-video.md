# Apps and video

Ports that are not games: an existing desktop app on PortMaster's Python runtime, a new app written
for the handhelds on the LÖVE runtime, and video playback through libmpv. Evidence comes from two of
the author's ports: Jellyfin MPV Shim (a Python Jellyfin client that plays through mpv, Oct 2026)
and a Home Assistant client written in Lua on `love_11.5` (Oct 2026, PC-tested only).

## Choosing a route

- **Android apps do not run** on PortMaster firmwares (no Android runtime, no Java layer). The Home
  Assistant companion app (Kotlin) was not an option; see
  [choosing an approach](../choosing-an-approach.md) for the same question with games.
- **An existing desktop app in an interpreted language** is the cheapest route when its toolkit
  can draw on the handheld. jellyfin-mpv-shim is Python and draws its whole UI inside mpv (Lua
  scripts plus Pillow images, driven by mpv's `GAMEPAD_*` keys), so only mpv needed porting.
  Apps built on GTK or Qt would have to bring their whole toolkit.
- **A new app on `love_11.5`** fits when no suitable client exists: LÖVE gives a fullscreen window,
  pad input, fonts, images, threads and LuaSocket, and nothing has to be compiled. The Home
  Assistant client was modelled on the official app's Wear OS screens, which are already designed
  for a small screen and few buttons. A WebKit build of the web frontend was the alternative and
  was not chosen.

## Video through libmpv

[mpv/](../../mpv/) has the SDL2 display context for mpv and the full aarch64 cross build of
libmpv (mpv 0.41.0, FFmpeg 7.1.2, libplacebo, libass, LuaJIT; glibc 2.29 at most, links only libc,
libm, libdl, libpthread, libgcc_s, zlib and the firmware's `libSDL2-2.0.so.0`). The README lists the
licences to ship. Why it is built that way:

- **Give mpv an SDL2 display context, not its DRM one.** Knulli draws through a Mali **fbdev** SDL
  driver and ROCKNIX through **Wayland/Sway**, so `gpu-context=drm` would fail on both (not tried). The same lesson
  applies to any program with its own window code: let the firmware's SDL2 open the window (see
  [graphics](../graphics.md) for glxsdl, the same idea for GLX programs).
- The context opens the fullscreen window at `DISPLAY_WIDTH` x `DISPLAY_HEIGHT` (FULLSCREEN_DESKTOP
  only when that matches the mode), asks for GLES 3.0 with a 2.0 fallback, and looks GL functions
  up with `dlsym` on `libGLESv2`/`libEGL` when `SDL_GL_GetProcAddress` returns NULL (Mali answers
  NULL for core functions). It restores the crash handlers after the window and GL context are
  created.
- **One SDL event loop per process.** mpv's own `--input-gamepad` thread refuses to start once SDL
  events are initialised, and two loops would steal each other's events. Build with
  `-Dsdl2-gamepad=disabled` and send controller buttons from the context as mpv's positional
  `GAMEPAD_*` keys.
- **Audio only through `ao=sdl`** (`-Dalsa=disabled`), so audio goes wherever the firmware's SDL2
  sends it.
- **No fontconfig**: libass needs the default subtitle font at `<mpv config dir>/subfont.ttf`.
- **Text fields**: the context also makes printable characters from `SDL_KEYDOWN` when no
  `SDL_TEXTINPUT` is queued, so gptokeyb's keys can type. In the shim's UI, A/Enter starts editing a
  field, Enter commits (and submits the form), B cancels.

### Building libmpv

The cross build (host clang + lld against the bullseye sysroot, never Ubuntu's gcc cross
compiler, which gave a libmpv needing GLIBC_2.38) and its CMake and Meson traps are on
[building native code](../building-native-code.md#cross-building-with-clang). The whole build takes
about 25 minutes.

### mpv settings for the handhelds

- `vo=gpu` + `profile=fast` + a 48 MB demuxer cache, seeded into the app's `mpv.conf` on the first
  launch with `cp -n defaults/*` so a user's edits survive updates.
- `vo=gpu-next` drew stripes with Ubuntu's libplacebo 6.338 on Mesa 24 on the PC while `vo=gpu` was
  correct. Whether `gpu-next` works on the Mali blob is untested (Oct 2026).
- Let the server transcode what the CPU cannot decode: the port asks for server-side transcoding of
  HEVC, AV1, 10-bit, HDR and anything above 1080p.
- Map buttons in a shipped `input.conf`: the top face button stops playback
  (`GAMEPAD_ACTION_UP keypress q`); back only hides the on-screen controls (PC-tested; not yet
  confirmed on a device, Oct 2026).

### Measured

RG Cube XX (Allwinner H700, 4 x Cortex-A53, Mali-G31, Knulli, 720x720, 1 GB), 5 Oct 2026:

| What | Result |
|--|--|
| First launch | harbourmaster fetched `python_3.11` (31.8 MB); app up in ~8 s, "SDL mali driver, 720x720", controller found |
| 720p episode, cast from the phone app | real time, ~25% CPU, 169 MB RSS |
| Big Buck Bunny 1080p30 H.264, 3.5 Mbit/s, direct play | real time, ~213% of one core (of 400%) at 1.51 GHz, 297 MB RSS, 498 MB available, no desync |
| Quit hotkey (`pkill -9` on the app's name) | app gone, runtime unmounted, launcher finished; the firmware's own Python service (wsdd) kept running |

Other firmwares and devices are untested.

## Python apps on `python_3.11`

PortMaster's `python_3.11` runtime is mounted by the launcher and set as `PYTHONHOME`; how
runtimes are declared and mounted is on [packaging](../packaging.md). Gaps found:

- **No libssl and no libsqlite in the runtime.** `_ssl` needs `libssl.so.1.1` + `libcrypto.so.1.1`,
  `_sqlite3` needs `libsqlite3.so.0`. Ship bullseye's in the port's `libs/` and put that folder on
  `LD_LIBRARY_PATH` before the runtime's own `libs`.
- **`ctypes.util.find_library` cannot work on the devices** (no ldconfig cache entry for the port's
  libraries, no gcc). Override it in a tiny entry script that returns the bundled paths.
- **Guard that entry script with `if __name__ == "__main__"`.** `multiprocessing` in spawn mode
  re-imports the main module; without the guard the first run started a second copy of the app.
- Resolve the app's Python dependencies from its own `pyproject` into a pinned list, and keep an
  update script that rebuilds the release from the newest upstream tag (fetch, wheels, staging,
  lint, zip, smoke test), so following upstream releases is one command.

## Testing an app on the PC

The general methods are on [testing and debugging](../testing-and-debugging.md). For a Python app
with libmpv:

- Run the aarch64 release under `qemu-aarch64` with `QEMU_LD_PREFIX=<arm64 chroot>`, PortMaster's
  extracted `python_3.11` as `PYTHONHOME`, SDL on Xvfb with Mesa. Add `gpu-sw=yes` to the test's
  `mpv.conf`, or mpv refuses llvmpipe.
- **qemu-user does not translate evdev ioctls** (`EVIOCGID` returns ENOTTY), so SDL never sees a
  gamepad there. Build the same mpv natively for x86_64 and drive it with a virtual uinput pad
  ([devtools/device/](../../devtools/device/) has `uinput_pad.py`). In WSL this works as root
  (`/dev/input/event0`) once a fake udev entry `/run/udev/data/c13:<minor>` holds
  `E:ID_INPUT_JOYSTICK=1`.
- **Stand in for the other side with a local server.** A portable Jellyfin server with test media
  made by ffmpeg's `lavfi`, driven through its REST API, replaced the phone app: Quick Connect
  approval, casts (`Sessions/{id}/Playing`), play state. The Home Assistant client was tested
  against a local Home Assistant install with test logins, with scripts for login, a tour of the
  screens, reconnects and error cases.
- **Screenshots on Mali fbdev**: SDL's fbdev driver flips between two framebuffer pages. Read the
  page that is on screen from the pan offset of `fb0`; always grabbing one page gives a stale frame
  every other time.

## An own app on LÖVE

The runtime itself (launcher variables, fitting the screen, compatibility shims) is on
[LÖVE](love.md). What an app, as opposed to a game, needs:

- **Draw only when something changed.** Replace `love.run` with an event-driven loop that redraws
  only when the UI is dirty or animating, instead of a game loop that redraws a mostly still
  screen at 60 fps.
- **Keep the network off the UI thread.** The Home Assistant client runs its WebSocket client
  (RFC 6455 on LuaSocket) in one `love.thread` and two HTTP workers in others; images are decoded
  in the workers too. A mDNS query finds servers on the local network.
- **No TLS out of the box** (as of Oct 2026): LuaSocket in the runtime speaks plain TCP only, so
  `ws://` and `http://` work and `https`/`wss` need luasec plus an OpenSSL or mbedTLS built for
  aarch64. Not done yet.
- **Scale the UI from one design size**: scale = min(w / 640, h / 480) covers every handheld screen.
- **Bring an on-screen keyboard** for logins and text (the client has its own).
- Reconnect with backoff (1 to 30 s) and a ping/pong heartbeat. Whether Wi-Fi is back after a
  suspend and resume is untested.
