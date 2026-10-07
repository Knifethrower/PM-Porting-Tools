# Performance and memory

How to find out what limits a port's frame rate and memory on the handhelds, and the fixes that
worked: threads, clocks, per-frame copies, CPU traps, and fitting a game into a 1 GB device.
Engine-specific knobs live on the engine pages; this page has the general method and the numbers
that back it.

Devices in the measurements below: RG353V (RK3566, 4 x Cortex-A55 at 1992 MHz, Mali-G52, 2 GB,
dArkOS), RG351P (RK3326, 4 x Cortex-A35, Mali-G31, 1 GB, ROCKNIX), RG Cube XX (Allwinner H700,
4 x Cortex-A53 at 1.5 GHz, Mali-G31, 1 GB, Knulli). See [devices and firmwares](devices-and-firmwares.md).

## Measure on the device before optimising

- **Profile on the device first.** A PC cannot show what an A53 and a Mali blob driver do with the
  same code. On Torus Trooper, three renderer rewrites were made against a bottleneck inferred from
  PC tests and device FPS numbers (draw calls); none helped and one made it far worse. One profile
  on the device found the real cause in a minute: D's `real` type is software arithmetic on
  aarch64 ([native ports: D programs](engines/native.md#d-programs)).
- **Profile the game's own code before theorising.** On Offspring Fling (Ruffle, Oct 2026) the
  obvious suspect for slow loading (level XML parsed twice, as the decompile suggested) was a
  decompiler artefact worth ~10% of init. The script profile showed 55% of init decoding 120
  bundled replays just to count their frames, and every level start spending 1 s in a pixel
  recolour loop. Fixing those two: PC init 1.8 -> 0.9 s, level load 0.8 -> 0.35 s, peak RSS -180 MB.
- **Measure variants before changing game code.** On Teslagrad (Unity 4, Oct 2026) Unity's
  `backgroundLoadingPriority` Normal/Low made the menu appear 11-14 s later, and skipping
  `Shader.WarmupAllShaders` saved ~1 s in a single run (within noise). Loading under box64 + Mono JIT is not fixed by
  Unity's knobs.
- **Before a big rework, measure what share it can win.** Native ARM64 Mono for a Unity game under
  box64 would have been weeks of work; the profile showed C# is only ~24% of the main thread, so the
  ceiling was low and it was parked.

## Measuring frame rate

### Where the numbers come from

| Source | What | Where |
|--|--|--|
| `GLESPASS_FPSFILE` | FPS per second, counted at swap | glespass shim for GLES-converted Unity games ([shim](../shim/)) |
| `CRUSTY_FPS=1` | `[CRUSTY] FPS: ...` in the game's stdout (so in `unity.log`) | Westonpack's crusty; printed nothing on an A133P device |
| fpslog preload | fps, longest frame, framebuffer grabs and readbacks per second, for any box64 game | [fpslog](../devtools/profiling/fpslog/) |
| glcount probe | draws per frame, drawing time, frame dumps, fixed clock | [device-sampler](../devtools/profiling/device-sampler/) |
| per-phase timing patch | wall time and main-thread CPU time for logic, frame building, draw, swap | `fpslog_patch.py` in the same folder |
| the game's own clock | in-game timer or script profiler | engine-specific (Ruffle: `RUFFLE_AS3_PROF=1`, `RUFFLE_SDL_TIMING=1`) |

[`benchfps.py`](../devtools/profiling/) gives median, mean, min, max and stdev over a window of an
fps log.

- **Wall time far above CPU time means waiting** (GPU, vsync); equal means computing.
- **30 fps on these devices is often a vsync step, not the game's speed.** On the RG Cube XX the
  KMS flip path syncs even with Unity's `vSyncCount` patched to 0 and `SDL_RENDER_VSYNC=0`: any
  frame over 16.7 ms lands on 30. Compare variants by **CPU time per frame** (`utime + stime` from
  `/proc/<pid>/stat` over the window, divided by the frames counted) and by the busiest thread's
  share (`/proc/<pid>/task/*/stat`), not by fps.
- **Measure game speed by the game's clock, not by frames.** FlashPunk's fixed-step loop runs n
  updates + 1 render per timer event inside one callback; Ruffle's frames/s and logic ms do not see
  it. In Unity, check that game time keeps up below the target frame rate
  ([Unity: game time vs FPS](engines/unity.md)): Gone Home ran in slow motion below 30 fps while
  audio played in real time.

### A/B method

- Same save, walk to a fixed spot, **stand still 60 s**, quit; compare medians over the plateau.
  Standing-still noise on Night in the Woods (RG353V): stdev ~1 fps. Differences under 1 fps are
  noise.
- **Thermal drift**: the RK3566 throttles 1992 -> 1608 MHz after about a minute, and FPS can drift
  down late in a run as the SoC heats. Log the minimum CPU MHz per run.
- **Check for stray load at startup.** dArkOS's `ogage` hotkey daemon once got stuck at 100% CPU
  and silently cut FPS by a third for several runs (title 58 -> 45, game 20 -> 13). Have the test
  launcher log uptime, load, SoC temperatures and `top` at startup, and end leftover game processes
  from an earlier run.
- Write one summary line per run: profile, FPS p10 and median, minimum CPU MHz, scenes reached.
- For repeatable runs, script the input (see [testing and debugging](testing-and-debugging.md)):
  two builds then get the same 70 s session.

## Profiling

### Native code

[device-sampler](../devtools/profiling/device-sampler/) has a sampling profiler that is linked into
a test build and starts by itself (SIGPROF every 2 ms of CPU time, program counter and thread id
to a file, `/proc/self/maps` copied alongside), and a report script that splits samples per thread,
per mapped file and per function. It needs no tools on the device. Link the test binary
unstripped; for a system library (the GL driver), copy it from the device and resolve against its
dynamic symbols.

- Run test builds from a RAM copy (`/tmp`): Knulli's data partition is exFAT through FUSE, and a
  write per sample through it distorts the result.
- Pause the front end with `killall -STOP emulationstation` (and `-CONT` after) rather than stopping
  it. It keeps the audio device, so such a run has no sound.

### Under box64

box64 runs x86_64 Linux games on aarch64 by translating their code (see [box64](box64.md)).
Profiling it needs a different method, because the program counters point into translated code.
The tools are in [profiling](../devtools/profiling/) (`nitwprof.c`, `analyze.py`, `opstats.py`,
`ctlflow.py`):

- **Sample with perf events, never ptrace**: `perf_event_open` with `PERF_COUNT_SW_TASK_CLOCK` at
  1 kHz per thread (IP + TID) never stops threads; ptrace would fight box64's and Mono's signal use.
  Needs root; dArkOS has perf events with paranoid 2, which is fine as root.
- **Map translated code back to x86 with the perf map**: `BOX64_DYNAREC_PERFMAP=1` writes one line
  per translated x86 instruction (native address, ARM instruction count, module/symbol + offset,
  mnemonic). About 200 MB per session; the default location `/tmp` is RAM, so the author's box64
  build was patched to take a path from `BOX64_PERFMAP_FILE`.
- **Turn DynaCache off while profiling** (`BOX64_DYNACACHE=0`): box64 0.4.x reloads translated code
  from `$HOME/.cache/box64` without writing perf-map lines. DynaCache itself did not change FPS.
- **Bucket samples** per thread into x86 module (player, Mono JIT code, libmono, FMOD), box64
  internals (translation, block lookup and linking, signals, library-call bridge, interpreter),
  native libraries and kernel. `opstats.py` counts samples per x86 mnemonic with its ARM expansion,
  `ctlflow.py` splits calls, jumps and returns by form.
- **Sample thread states too**: every 2 ms for the main thread, the render thread and the GPU
  driver's job thread, read `/proc/<pid>/task/<tid>/{stat,syscall,wchan}` (running vs blocked,
  syscall number, futex address, wait channel). Read GPU load from `/sys/class/devfreq/<gpu>/load`.
  "Both game threads idle" is the number to drive down.

Findings on Night in the Woods (Unity 5.6, Mono, FMOD; RG353V at 1992 MHz):

- All threads: FMOD 37% (about one core), Unity engine 28%, Mali driver 12%, C# JIT code 7%,
  kernel 6%, libmono 2%, **box64's own overhead ~1%**.
- Main thread (sets the FPS, ~77% busy): engine 59%, C# 24%, libmono 6%. ~31% of it is control
  flow (direct and indirect calls, returns, jumps), spread very flat: the hottest site was 0.28%.
- GPU load 15-22%: not GPU-bound. The render thread spends 52% of its time in the Mali driver.
- Conclusion: the frame rate is set by the amount of engine + C# work the A55 cores must run under
  emulation. box64 is already efficient; dynarec work on calls was estimated at 5-10% at best for
  weeks of risky work and not done. The wins came from threads and cores (next section). box64
  settings that were tried are on the [box64](box64.md) page.

## Threads, clocks and cores

Under box64 a Unity game is **CPU-bound on its main thread** (Mono scripts + engine); GPU and render
thread have headroom. On Night in the Woods turning effects off first seemed to change nothing,
although a later standing-still A/B on the RG353V with FXAA, Bloom and NoiseAndGrain off went from
19.5 to 25 fps ([Unity](engines/unity.md#performance)). The steps that helped, each stated once
(RK3566, RG353V, Sep 2026 unless noted):

- **Threaded rendering.** Unity draws on a separate render thread by default; under Westonpack's
  crusty the GL context must be handed to that thread for real (see [graphics](graphics.md)). Night
  in the Woods: play median 11 -> 16.5 fps, Mae Street ~12 -> 22-25. The largest single step.
- **Performance governor** for CPU and GPU during play, restored on exit. No separate figure on
  Night in the Woods; on the RG Cube XX (Sep 2026): -19% CPU per frame at 30 fps (schedutil dips
  mid-frame), little at 60. It does not help where the cost is latency: on the Mali-G31 blob a
  GPU->CPU readback costs ~10 ms whatever the CPU and GPU clocks.
- **Thread pinning**: the main thread and the render thread each on their own core, everything else
  on the remaining cores. FMOD's real-time mixer threads (SCHED_FIFO 95/99) otherwise pre-empt the
  two frame-critical threads. No separate before/after was recorded; with threading and pinning
  together the play median is 16.5 and the fixed benchmark spot 20 fps (below).
- **Put the GPU driver's threads (`mali-*`, especially `mali-cmar-backe`, the job submission thread)
  on the render thread's core**, not with the rest. Next to FMOD's RT threads they sit runnable but
  starved, the render thread waits for them and the main thread waits for the render thread. This
  cut "both game threads idle" from 21% to 13.5% but did not change FPS on Night in the Woods
  (main-thread work was still the limit). Kept as the default.

Results on Night in the Woods, RG353V (threaded + pinned): title 60, rooms 25-33, Mae Street 22-25,
Underhill 15-18, play median 16.5, fixed benchmark spot 20 fps. RG351P (RK3326, ROCKNIX, black
screen at the time): ~20-27 title, 6-10 in game.

### Pinning in a launcher

- A background shell loop in the launcher runs `taskset -p` on `/proc/<pid>/task/*`. Main thread:
  tid == pid. Render thread: its tid written by the GL shim (`GLESPASS_RENDERTID`).
- Rank cores by `cpufreq/cpuinfo_max_freq`, fastest first, so big.LITTLE chips get the two game
  threads on big cores.
- Find the game's pid by scanning `/proc/*/comm` for the x86 program's name, cut to 15 characters
  ([box64: the process name](box64.md#the-process-name)).
- The shell traps this loop meets on the firmwares (no `nproc`, `paste` is a log uploader, busybox
  `pgrep -x` matching nothing on ROCKNIX) are on
  [devices and firmwares](devices-and-firmwares.md#rocknix).
- Safe defaults for firmwares that manage their own clocks (ROCKNIX): no governor change, no
  pinning.

## Per-frame GPU costs

The details belong to [graphics](graphics.md); the rules that matter for speed:

- **Draw calls are expensive on the Mali blobs** (a fixed ~30-100 us each), so immediate-mode games
  through gl4es (a library that implements desktop OpenGL on top of GLES) crawl; a native GLES 2
  layer took Cube from 8-22 to 60 fps ([graphics: native GLES 2](graphics.md#native-gles-2-when-you-have-the-source)).
- The other Mali blob costs (draw-call tuning on a guess, `glBufferSubData` copying a whole VBO,
  ~10 ms per GPU->CPU readback) are on [graphics: Mali driver lessons](graphics.md#mali-driver-lessons).
  Count readbacks in your game and remove what forces them: in Offspring Fling the cause was text
  drawn into a bitmap and mixed CPU/GPU blits.
- **Framebuffer copies through gl4es** become a CPU readback + re-upload with a pipeline stall
  unless the texture format matches what the driver reads (`LIBGL_NOBGRA=1` makes Unity's textures
  RGBA; when to set it: [graphics](graphics.md#what-it-costs)). RG Cube XX, Ittle Dew (Sep 2026):
  capped 30 -> flat 60 fps with the same two copies per frame. RG351P, RK3326 libmali, glxsdl
  runtime (Oct 2026): 25.0 fps / 72.6 ms CPU per frame -> 33.1-33.5 fps / 55 ms. Unity 4 specifics
  (GrabPass, image effects on screen cameras) are on [Unity 4](engines/unity-4.md).
- **Fixed-size render targets**: 2D games often render into an RT of their design resolution
  (1280x720) and scale it down: 3-6x the pixels of a 640x480 or 480x320 screen (4-7x the displayed
  16:9 area), and every copy scales with it.
  Sizing the RT to the displayed area cuts fill rate, copies and memory, but without mipmaps the
  world aliases at 2:1 minification.
- **Rendering the game at the screen's 16:9 area** can matter more than any knob: Teslagrad on the
  RG Cube XX ran 20-30 fps at 720x720 and 50-60 at 720x405.

## CPU traps

### Software floating point

D's `real` and C/C++ `long double` are software arithmetic on aarch64 (Torus Trooper: 78.5% of CPU
in libm, 6-17 fps until fixed); cause, fix and measurements are on
[native ports: D programs](engines/native.md#d-programs).

### Managed code under box64

- **AOT instead of JIT**: Unity 4's Mono (2.6.5) can compile the game's own assemblies ahead of time
  into x86_64 `.dll.so` images that the player loads beside each DLL. Under box64 that is static
  code: no JIT at load, no call-site patching that invalidates translated blocks, and DynaCache can
  keep the translation. RG Cube XX (Sep 2026): -4% CPU per frame alone, -8% stacked with the other
  changes. Recipe on [Unity 4](engines/unity-4.md#performance).
- DynaCache (box64's cache of translated code, 12 s vs 18 s to the title) and the Mono GC settings
  that matter under box64 are on [box64](box64.md#dynacache).
- **After a long tick, collect once.** Full collections between ticks do nothing for a multi-second
  init tick and cost 0.4-0.6 s each on an A53. One full collection + `malloc_trim` right after any
  tick longer than 1 s (init, level start) returns the peak at once (Offspring Fling, Oct 2026).
  Logging those long ticks at info level puts loading times in every device log for free.

### Audio

FMOD used about one core on Night in the Woods (37% of all CPU). It does not limit FPS directly when
the threads are pinned, but it adds heat and throttling. Fewer real channels or 44.1 kHz source audio
could cut it (not tried). Re-encoding is on [Unity](engines/unity.md).

## Memory and 1 GB devices

### The budget

- Mali GPUs share system memory: textures the driver or gl4es holds count against the same RAM as
  the game.
- What is left on a 1 GB device once the front end is resident, which firmwares have swap, and how
  to detect a 1 GB device (`MemTotal` < 1.5 GiB): [devices and firmwares](devices-and-firmwares.md#ram-classes).
- ROCKNIX on RK3326 has zram as a module and `zramctl`: a 256 MB zram swap costs nothing idle and
  takes cold pages early (16 MB used with 140 MB still free), which is the way to give 1 GB ROCKNIX
  devices larger textures.
- A Unity 5 game under box64 for scale: Night in the Woods had ~840 MB RSS on the RG353V, which is
  why it asks for 2 GB (`"2gb"` in port.json, see [packaging](packaging.md)).

### What a thrashing or killed game looks like

- Exit 137 (SIGKILL) after the first scenes, or a very long load, sometimes with the world drawn
  black, then a kill. RG351P (Ittle Dew, halved textures): OOM-killed at anon 532 + file 118 MB
  after ~2 minutes of thrashing at ~100 MB available.
- Exit 137 right after Select+Start presses in the gptokeyb log is the quit hotkey, not a crash.
- Confirm with `dmesg | grep -iE "out of memory|oom-kill|killed process"` after the run. `dmesg`
  keeps earlier kills: filter by the uptime at launch.
- A rising major-fault rate (`/proc/vmstat`) separates thrashing from a real hang;
  [`thread_sample.sh`](../devtools/device/) prints it.

### Measure where the memory goes

1. **Log memory on the device while debugging**: a background loop in the launcher writing
   `MemAvailable`, `SwapFree` and the game's `VmRSS` every 5 s, the biggest processes from
   `/proc/*/status` before the game, and the `dmesg` OOM lines after it. Remove it from the release.
2. **Split RSS by mapping** from `/proc/<pid>/smaps`: anonymous, box64 binary, x86 code (player,
   libmono, `libs.x64`), graphics libraries, GPU device mappings (`/dev/mali0`), game files, other
   libraries; plus Weston and Xwayland if Westonpack is used. Plain awk works with mawk and gawk;
   [`smaps_summary.py`](../devtools/device/) does it per mapping. Logging a split whenever RSS grew
   32 MB showed the growth by phase.
3. **box64's own allocations** are printed at exit with `BOX64_LOG>=1` (box64 0.4.4:
   `Allocation: - dynarec: N kio - customMalloc: N kio - jump table: N kio`), but only on a normal
   exit through the game's own Quit: a kill (Select+Start, OOM) skips it. Dynarec blocks are 2 MB
   mmaps and show as anonymous memory in smaps.
4. **Inventory the assets** before guessing: sum the byte size per class per file and note which
   textures are readable and how audio is stored. [`tex_inventory.py`](../unity/converters/unity4/)
   lists the large textures of a Unity 4 game with size, format and bytes;
   [`unityport`](../unity/unityport/) can survey a Unity 5.x to 2017.3 game.

Findings from Ittle Dew (Unity 4, Sep 2026):

- **box64 itself is not where the memory goes**: on the RG351P (quarter-size textures, 607 MB RSS)
  anon 470, box64 binary ~13, x86 code ~16, graphics libraries ~30 MB; Weston ~12 and Xwayland
  ~30 MB separately. The game's heap is the cost.
- **gl4es decoding DXT textures into RAM** (Mali has no S3TC) was the biggest single item, ~270 MB
  of peak RSS on the PC ([graphics: what gl4es costs](graphics.md#what-it-costs)).
- With shrunk textures the remaining ~500 MB RSS was ~80% runtime (Unity heaps, Mono, box64
  dynablocks, gl4es). Audio (all Ogg, compressed in memory, 18.5 MB) and meshes (6 MB) were dead
  ends.
- Dropping Westonpack for an in-process X11 stand-in freed +20 MB on the RG Cube XX and +35 MB on
  the RG351P (see [graphics](graphics.md)).

### Reducing it

- **Textures first.** Order of preference:
  - ASTC on the GPU where the runtime can feed it: 1 byte per pixel (4x4 blocks), stays compressed.
    gl4es decodes DXT1 to 16-bit pixels (4x the file) and uncompressed textures stay 3-4 bytes per
    pixel. Ascendant (Unity 4, Oct 2026): DXT1 ~508 MB decoded + 286 MB uncompressed + 147 MB
    became 402 MB of ASTC.
  - 16-bit textures through gl4es (`LIBGL_AVOID16BITS=0`): 222 MB less peak RSS on the PC for
    Ittle Dew, no visible loss ([graphics](graphics.md#what-it-costs)).
  - Offline downscaling of large textures on the device at first launch. Ittle Dew: 189 textures,
    320 -> 80 MB of texture data; peak memory under gl4es 1008 -> 644 MB on the PC. Half size still
    ran out of memory on the RG351P; quarter size fit. The tool and its checks (sprites that use
    pixel sizes, pixel-perfect sprite managers) are on [Unity 4](engines/unity-4.md).
  - **Shrink by residency, not by size.** List the large textures and ask which are resident all
    game (scene-referenced sheets, portraits) and which load on demand per zone. Ittle Dew's 25 room
    tile atlases cost 4 MB of RSS at full size because one zone's atlas is loaded at a time, and
    they are what the player looks at. A size cap ("everything at most 1024 px") can cost more than
    a uniform halving when mid-size textures are numerous: compare the bytes per policy on the PC
    first.
- **Not gl4es's `LIBGL_SHRINK`** for Unity 4: it breaks render targets or saves little
  ([graphics: defaults and settings](graphics.md#defaults-and-settings)).
- **PC RSS is no guide for texture memory**: llvmpipe stores ASTC decoded at 32 bit but DXT
  compressed. Measure on the device.
- Stop or shrink what else is resident: a test with the front end stopped tells whether the game
  fits at all (half-size textures on the RG351P reached 700 MB RSS and fit only with
  EmulationStation stopped or zram on).
- Two half-size steps on the PC cost a little more blur than one quarter-size step (a 2x2 filter
  per step vs one F x F filter: +0.5 dB median for the single step).
- Other engines: LÖVE, Godot and GameMaker memory notes are on their pages
  ([LÖVE](engines/love.md), [Godot](engines/godot.md), [GameMaker](engines/gamemaker.md)).

### Faking a 1 GB device on the PC

Run the first-run setup with a fake `/proc/meminfo`: `unshare -m` plus a bind mount over
`/proc/meminfo`, so the "1 GB" branch of a setup script runs on the PC. See
[testing and debugging](testing-and-debugging.md).
