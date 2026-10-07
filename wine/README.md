# Wine on PortMaster: tools

Tools from the Box64 + Wine 11.0 runtime, used by the
Peggle and Word Viewer ports.

| Path | What |
|--|--|
| `xbridge/` | Westonpack's headless Weston only shows GL programs (crusty). Wine draws GDI and software DirectDraw with plain X11, which ends up nowhere. xbridge copies the visible top-level windows of Weston's rootless Xwayland (XShm, stacking order, XFixes cursor) to the screen through SDL2 (dlopen'd), scaled, vsynced, and grabs gptokeyb's virtual keyboard/mouse (EVIOCGRAB) and injects it with XTest. `XBRIDGE_DUMP=out.ppm` writes one frame; `XBRIDGE_NOVIDEO=1` + `XBRIDGE_INPUT=<fifo>` tests input on the PC. `build.sh`: aarch64 (glibc 2.28, Westonpack's X11 libs) + x86_64. |
| `glxcheck/` | Native GLX check without box64 or Wine: loads the libGL.so.1 the library path finds, creates a context, prints vendor/renderer/version. Exit 0 = OpenGL works on that display. |
| `tests/` | Three small 32-bit Windows programs (GDI, DirectDraw, Direct3D 9) that draw, count input and write `results.txt`: what the Wine Test port runs. `build.sh` needs `gcc-mingw-w64-i686`. |
| `box64/` | `wine-reserve.patch` for box64 0.4.4: reserve the low 32-bit area up to box64's own load address like wine-preloader does, so 32-bit programs with a fixed image base above 0x30110000 (Word Viewer 2003) can be mapped. `build_box64.sh` applies it on a copy of the portable build. |
| `link_prefix.py` | `link_prefix.py <prefix> <wine dir>`: replaces prefix files that are byte-identical to a Wine builtin DLL with relative symlinks into the runtime, so a shipped prefix stays small. |
| `bin2iso.py` | MODE1/2352 `.bin` (one data track) to `.iso`, for installing from a user's disc image. |
| `send_events.py` | Writes raw `struct input_event` records (pointer moves, clicks, a key) like gptokeyb's device: feed into xbridge's `XBRIDGE_INPUT` FIFO for the PC input test. |
