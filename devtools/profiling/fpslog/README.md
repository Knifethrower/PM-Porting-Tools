# fpslog (x86_64 preload for box64 games)

From Ittle Dew. Counts `glXSwapBuffers` in the emulated game and prints
`[fps] N.N (longest frame N ms, N grabs WxH, N readbacks)` once a second to stderr (Unity 4 sends
stderr to its `-logFile`), independent of crusty's `CRUSTY_FPS`. Options in the source header:
`FPSLOG_TEX=1` (texture upload / copy shapes), `FPSLOG_FIXBGRA`, `FPSLOG_FINISH0`.

```bash
gcc -O2 -shared -fPIC -o libfpslog.so fpslog.c -ldl      # x86_64, on the PC
```

Ship it in `libs.x64/` of a test build only and add it to the box64 preload:
`BOX64_LD_PRELOAD=libfpslog.so` (or append with `:` after an existing one). License: 0BSD.
