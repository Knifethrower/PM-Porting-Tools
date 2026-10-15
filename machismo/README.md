# machismo: running Apple Silicon (arm64 Mach-O) games natively

[machismo](https://github.com/bmdhacks/machismo) loads a macOS arm64 game on Linux aarch64
and maps its dylibs to Linux libraries. First check a game with `macsurvey/` (candidate or not,
draft `dylib_map.conf`). From Spaghetti Celesti and Tummy Bonbons (2026-10-01/02), both playing on
the RG Cube XX.

| Path | What |
|--|--|
| `scan_isa.sh <binary>` | ARMv8.1+ instructions in the game that a Cortex-A53 can't run (SIGILL on the device): LSE atomics, SHA3 EOR3/BCAX... Each must be emulated by machismo's `isa_emul.c`. Spaghetti Celesti: 17 LSE + 4 EOR3 + 1 BCAX. |
| `patches/isa-emul-eor3.patch` | machismo: emulate EOR3 (BCAX was already there). GPLv3 like machismo; worth sending upstream. |
| `patches/eh-frame-native-register.patch` | machismo: don't register the native `.eh_frame` with `__register_frame` when the `_dl_find_object` hook serves it: GCC 13+ libgcc parses registered tables eagerly and ran off its unterminated end (crash at the first C++ exception, Tummy Bonbons on the Cube XX). GPLv3; upstream candidate. |
| `stdio_trace.c` | LD_PRELOAD logger of fopen/fread/fseek/ftell/fclose on matching paths: what a game reads, in which order, how much. Any game, not only machismo. |

Tips from the two ports:
- Syscall trace of the dylib/library opens under qemu: `QEMU_STRACE=1` (Spaghetti Celesti
  `pctest/trace_dlopen.sh`).
- Run the binary by its absolute path (Tummy Bonbons looked up its resources relative to argv[0]).
- LuaJIT inside the game may need a trampoline (Tummy Bonbons).
- Clone Linux reference ports with `core.autocrlf=false`: CRLF broke `dylib_map` paths.
