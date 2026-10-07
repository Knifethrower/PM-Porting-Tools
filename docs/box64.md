# box64

box64 runs x86_64 Linux programs on aarch64 by translating their code at run time (a "dynarec") and calling
native aarch64 versions of common libraries (libc, libGL, libX11, SDL2 ...) instead of emulating them. This page
covers building it for the handhelds, the settings that matter for games with a JIT (Unity's Mono), libraries,
what it costs in memory and CPU, and debugging crashes under it. Profiling a game under box64 is on
[performance and memory](performance-and-memory.md); Windows games through box64 + Wine are on
[Windows, Wine and Flash](engines/windows-wine-and-flash.md).

## When to use it

- A game with a **Linux x86_64 build** and no source runs under box64 with its own build. For Unity games this is
  the first route to try ([choosing an approach](choosing-an-approach.md)).
- **32-bit x86 builds** need box86 and a 32-bit userland, which not every firmware has. Ittle Dew's Steam build
  (Unity 4.3, 32-bit only) was not portable this way; Unity 4 games can move onto a 64-bit player instead
  ([Unity 4](engines/unity-4.md)).
- **Windows builds** would need Wine on top (box86 + 32-bit userland, or box64 with Wine's WoW64 mode). A native
  Linux build through box64 is always the better route when one exists.

## Building it

Ports ship their own `box64` binary in the port folder. The author's builds are box64 0.4.4 (Sep 2026). The fork
[Knifethrower/box64-portmaster](https://github.com/Knifethrower/box64-portmaster) is 0.4.4 plus the needed-libs
patch below; `wine/box64/build_box64.sh` applies the Wine patch on top.

Cross-compiled on an x86_64 Linux host (WSL):

```bash
cmake .. -DARM64=1 -DARM_DYNAREC=ON -DBAD_SIGNAL=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

- **`-DBAD_SIGNAL=ON` is required on Rockchip kernels.** They report `SEGV_MAPERR` for writes to pages the dynarec
  has write-protected; without it Mono's JIT crashes (seen at `libmono+0x35a96`).
- **Do not build with `-mcpu=cortex-a55`**: Cortex-A53/A35 cores (RK3326, H700) raise SIGILL on the atomics it
  emits. Use `-march=armv8-a -mtune=cortex-a55`; box64 detects LSE atomics at run time anyway. Details:
  [building native code](building-native-code.md#armv80-portability).
- **Old glibc**: a box64 linked against a current distribution's glibc does not start on older firmwares. Build
  against a Debian 10 sysroot (glibc 2.28) and check the result with
  `objdump -T box64 | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -1`; set port.json's `min_glibc` to the newest
  glibc any shipped binary needs (the RK3566-tuned build linked on Ubuntu 24.04 needed 2.39, the portable one
  2.28). The sysroot flags are on [building native code](building-native-code.md). The Wine runtime's build
  script ([wine/box64/build_box64.sh](../wine/box64/build_box64.sh)) is a complete example.
- **Do not link libc, libstdc++ or libgcc statically** into anything you ship (PortMaster-New's AGENTS.md forbids
  bundled or static core system libraries).
- Licence: box64 is MIT; ship its licence file with the port.

Patches in this repo:
- [box64/box64-needed-libs.patch](../box64/): a wrapped library whose native helper libraries are missing is
  initialised anyway. box64's libX11 wrapper needs libxcb, the libXcursor wrapper libXfixes and libXrender; with
  a libX11 stand-in and no X server none of them exist, and nothing calls them. Without the patch box64 stops
  with "Error: loading a needed libs in elf libX11.so.6". Needed for the glxsdl route
  ([graphics](graphics.md#without-westonpack-glx-on-the-firmwares-sdl2)).
- [wine/box64/wine-reserve.patch](../wine/box64/): reserves the low 32-bit area up to box64's own load address,
  as wine-preloader does, so 32-bit Windows programs with a fixed image base above 0x30110000 can be mapped.

Version notes:
- box64's dev branch (0.4.5, Sep 2026) was ~31% slower than 0.4.4 on Night in the Woods (13.9 vs 20.1 fps, RG353V).
  Not bisected; suspects were dynablock in-use tracking and HotPage changes. Benchmark a new version before
  switching.
- DynaCache file names do not include the box64 version, so a different build overwrites them: delete the cache
  when switching builds.

## Libraries

- box64 runs the game's own x86_64 libraries under emulation and replaces the libraries it knows (libc,
  pthread, libGL, libX11, SDL2, ...) with native aarch64 ones. The x86 C++ runtime is not one of those: ship
  x86_64 `libstdc++.so.6` and `libgcc_s.so.1` (from box64's `x64lib`) in `libs.x64/` and point box64 at it:

  ```bash
  BOX64_LD_LIBRARY_PATH=libs.x64:<Game>_Data/Mono/x86_64:<Game>_Data/Plugins/x86_64
  ```

  For Unity 4 players the Mono and Plugins folders are not needed: the player opens libmono and plugins by
  absolute path (check with `strace`).
- **Intercepting libc calls of the x86 code needs an x86 preload.** box64 resolves wrapped libc functions with
  `dlsym` on its own libc handle, so a native aarch64 `LD_PRELOAD` never sees the game's calls. Build a small
  x86_64 `.so`, ship it in `libs.x64/` and add `BOX64_LD_PRELOAD=libfoo.so` (several joined with `:`). Two such
  preloads in this repo: [sysvsem](../shim/sysvsem/) (System V semaphores for kernels built without them, see
  [devices and firmwares](devices-and-firmwares.md)) and [fpslog](../devtools/profiling/fpslog/) (fps and GL
  copy counts in the game's log). The same preload also works natively on the PC with `LD_PRELOAD`.
- Native stand-ins for wrapped libraries must keep what box64's wrapper touches: the libX11 wrapper dereferences
  `_XLockMutex_fn` / `_XUnlockMutex_fn` at load and reads `struct _XDisplay` fields, so a libX11 stand-in keeps
  the real Xlib struct layouts ([xstub](../shim/xstub/)).

## Settings

Pass box64 variables on the command line that starts the game. Under Westonpack that means as `env VAR=value`
arguments on the westonwrap line, because `$ESUDO` drops exported variables ([graphics](graphics.md#basics)).

### What the dynarec settings do

Values from box64 0.4.4's documentation (`docs/USAGE.md`, `src/include/env.h`):

| Variable | Default | What it does |
|--|--|--|
| `BOX64_DYNAREC_BIGBLOCK` | 2 | How far the dynarec extends a translated block. 0 is documented as the setting "for programs using lots of threads and JIT, like Unity"; 2 builds big blocks only in ELF code; 3 extends them into all memory, including JIT code, where Mono's patches invalidate them |
| `BOX64_DYNAREC_STRONGMEM` | 0 | Memory barriers that emulate x86's strong memory ordering; 1-3 add more barriers (slower, safer for threaded code) |
| `BOX64_DYNAREC_FORWARD` | 128 | Largest gap in bytes between the end of a block and a pending forward-jump target that the block still continues across (0, 128, 256, 512 or 1024) |
| `BOX64_DYNAREC_SAFEFLAGS` | 1 | How carefully CPU flags are kept across `CALL`/`RET`; 0 assumes they never need flags (faster), 2 handles every case |
| `BOX64_DYNAREC_CALLRET` | 2 on Linux | Return handling; 2 copes with returns into modified blocks |
| `BOX64_UNITY` | 0 | 1 marks a Unity game; on Linux that applies `BOX64_DYNAREC_STRONGMEM=1` |
| `BOX64_UNITYPLAYER` | 1 | Sets `BOX64_UNITY=1` when it finds a `UnityPlayer.so`. Players without one (Unity 4, and Unity 5.6 as in Night in the Woods) are not affected |

`BOX64_DYNAREC_DIRTY` and `BOX64_DYNAREC_NOHOTPAGE` govern pages that are both written and executed;
`BOX64_DYNAREC_WAIT=0` is the documented JIT option.

### Presets

Used with Night in the Woods (Unity 5.6, Mono, no `UnityPlayer.so`) on the RG353V, Sep 2026; all three stable.
Variables that only restate a default are left out:

| Preset | Variables | Meaning |
|--|--|--|
| safe | `BOX64_DYNAREC_STRONGMEM=1 BOX64_DYNAREC_BIGBLOCK=0` | What the tested `BOX64_UNITY=1` sets on Linux, plus no big blocks (the documented choice for a JIT) |
| fast | none (box64 0.4.4 defaults) | For a player that has a `UnityPlayer.so`, add `BOX64_UNITYPLAYER=0`, or box64 turns on `STRONGMEM=1` itself |
| faster (the default there) | `BOX64_DYNAREC_BIGBLOCK=3 BOX64_DYNAREC_FORWARD=1024 BOX64_DYNAREC_SAFEFLAGS=0` | Goes against the documented advice for JITs (big blocks in Mono's code), yet was stable on Night in the Woods; `BIGBLOCK=3` made no difference over 2 on Ittle Dew |

For a first release on firmwares you cannot test, start from `safe`, with no governor changes and no thread
pinning (ROCKNIX manages clocks itself and its core layout differs).

### What was tried with Mono (a JIT inside the emulated process)

Mono writes machine code at run time and patches it; box64 must notice and retranslate. Findings with box64 0.4.4:

| Setting | Result |
|--|--|
| `BOX64_DYNAREC_CALLRET=1` | **Never**: breaks Mono's JIT. The default (2) is fine with Mono |
| `BOX64_DYNAREC_ALIGNED_ATOMICS=1` | Crashed |
| `BOX64_DYNAREC_SEP=2` | No gain |
| `BOX64_DYNAREC_DIRTY=1` | Worse on Night in the Woods; a 30 s stall on Ittle Dew (RG Cube XX) |
| `BOX64_DYNAREC_BIGBLOCK=2` vs `3` | No difference on Ittle Dew (RG Cube XX, Sep 2026) |
| `BOX64_DYNAREC_FASTROUND=2` | No change |

Mono settings under box64:
- Never `GC_ENABLE_INCREMENTAL`: its mprotect write barriers fight the dynarec's page protection.
- Mono 2.6.5 (Unity 4) honours `GC_INITIAL_HEAP_SIZE` (fewer stop-the-world collections while loading),
  `MONO_INLINELIMIT` and `MONO_GENERIC_SHARING`; `MONO_DISABLE_SHM=1` stops its System V semaphore use
  (needed on kernels without System V IPC: [devices and firmwares](devices-and-firmwares.md#knulli)).
- Ahead-of-time compiled assemblies take most JIT work out of loading; the recipe for Unity 4's Mono is on
  [Unity 4](engines/unity-4.md).

### DynaCache

box64 0.4.x has `BOX64_DYNACACHE=1` by default: translated code is saved under `$HOME/.cache/box64` and reloaded
on the next launch. With `HOME` set to the port's config folder (as the lean launchers do) the cache lives in the
port. Measured on the RG Cube XX with Ittle Dew (Sep 2026): 12 s to the title on later launches vs 18 s cold. It
did not change fps on Night in the Woods. It hides code from the perf map, so profile with `BOX64_DYNACACHE=0`.

## The process name

box64 sets the kernel process name (`comm`) to the x86 program's name, truncated to 15 characters
(`IttleDew.x86_64`). Consequences:
- gptokeyb2's kill name is the game's name, not `box64`: `$GPTOKEYB2 "IttleDew" -c ...`.
  `pm_platform_helper "$GAMEDIR/box64"` still takes the real binary path.
- Find the game by scanning `/proc/*/comm` for that name, not with `pgrep` / `pkill`, which differ between
  firmwares ([devices and firmwares](devices-and-firmwares.md#shell-tools)).
- The main thread can become a zombie that hides `/proc/<pid>/fd`; read `/proc/<tid>/fd` of a live thread. A `Z`
  leader is normal.

## Signals and native libraries in the box64 process

box64 needs SIGSEGV for its dynarec write protection, and Mono uses signals of its own (signals 30 and 24 appear
with `BOX64_SHOWSEGV=1` in every run and are harmless).

**A native library loaded into the process must not take SIGSEGV away.** ArkOS-family SDL2 builds (dArkOS on the
RG353V) install SIGSEGV/BUS/ILL/FPE/ABRT console-restore handlers in `SDL_Init`, replacing box64's. Mono then died
at its first JIT write (box64's report: `si_code` -6 = `SI_TKILL`, "address" = the PID, a backtrace through
libSDL2 and `gsignal`). Fix in the native GLX layer: save the sigactions before `dlopen(SDL2)` / `SDL_Init` and
restore them once the window exists. Any native library that brings up SDL inside a box64 game needs the same.

## Memory

- **box64 prints its own allocations at exit** when `BOX64_LOG>=1`:
  `Allocation: - dynarec: N kio - customMalloc: N kio - jump table: N kio`. Only on a normal exit (the game's
  own quit); a kill (Select+Start through gptokeyb, the OOM killer) skips it. Dynarec blocks are 2 MB mmaps and
  show up as anonymous memory in `/proc/<pid>/smaps`.
- **box64 itself is not where the memory goes on 1 GB devices; the game's heap is** (Ittle Dew on the RG351P:
  box64 binary ~13 MB of 607 MB RSS). The full split and the method:
  [performance and memory](performance-and-memory.md#measure-where-the-memory-goes).
- Measure box64's part before assuming a route without it would save memory (for example an Android ARM player):
  most of that saving would come from compressed textures staying compressed, not from dropping box64.

## CPU

box64's own overhead is small: ~1% of all CPU time on Night in the Woods (RG353V, Sep 2026; translation,
lookups, signals). The frame rate is set by the engine and C# work it runs, so dynarec tuning has a low ceiling;
the wins came from threads and cores. Profile, method and the fps steps:
[profiling under box64](performance-and-memory.md#under-box64) and
[threads, clocks and cores](performance-and-memory.md#threads-clocks-and-cores).

## Debugging crashes

- `BOX64_LOG=1 BOX64_SHOWSEGV=1 BOX64_SHOWBT=1` for a crash; **read `si_code` first** (`SI_TKILL` means a thread sent the
  signal itself, not a memory fault). Add `BOX64_DLSYM_ERROR=1` for missing symbols. Very noisy:
  for bug reports and test builds only, not in a release launcher.
- A player that spins at 100% CPU before opening a window is often a missing kernel feature, not box64:
  `strace -c -f -p <pid>` for a few seconds shows what it retries (147k `semget = ENOSYS` on H700 kernels without
  System V IPC). See [devices and firmwares](devices-and-firmwares.md) and
  [testing and debugging](testing-and-debugging.md).
- For A/B runs a test launcher can take the box64 binary and extra box64 variables from the environment; keep
  that out of the release.
