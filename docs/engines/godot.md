# Godot

How to get Godot games onto PortMaster: Godot 3 projects with source, shipped Godot 3 and 4 packs
from Steam or GOG, and what to do when a shipped game does not fit into 1 GB of RAM. The tools are
in [godot/](../../godot/) ([README](../../godot/README.md)); this page is the overview and the
lessons behind them.

## How a Godot game is built

A Godot game is an engine binary plus a **pack** (`.pck`, or appended to the executable) that holds
every scene, script, texture and sound. The pack does not depend on the platform it was exported
for, so the usual port is: the game's pack, unchanged or patched, on an aarch64 engine binary of
**the same Godot version**. Nothing of the game is recompiled.

Find out what you have before anything else:

| Check | How | Why it matters |
|--|--|--|
| Godot major version | Pack header: format 1 = Godot 3, format 2 = Godot 4.0-4.3 | Picks the runtime (below). |
| Exact version | Version string in the executable (`3.5.1.stable.custom_build`, `4.3.1`) | Same minor version needed; `custom_build` means modules may be compiled in. |
| Compiled-in modules | Executable name and strings (`godot.windows.opt.64.godotsteam.exe`) | Singletons like `Steam` that a stock engine lacks (stubs, below). |
| Encryption | Pack flags | Only unencrypted packs are covered here. |
| Renderer | `project.binary`: `rendering/quality/driver/driver_name` (unset in Godot 3 = GLES3) | Godot 3 GLES3 games cannot simply fall back to GLES2 (below). |
| Scripts | `.gd` text or `.gdc` binary tokens | `.gdc` can be read with `gdc_tokens.py` / `gdc_print.py` (Godot 4.3). |

## Runtimes (as of Oct 2026)

| Godot | Route | Status in the author's ports |
|--|--|--|
| 2.x | PortMaster's `frt_2.1.6` | Did not run even a one-line script on the RG Cube XX (Knulli). Three Godot 2 games were dropped for this. |
| 3.x | PortMaster's `frt_3.5.2` (FRT 2.1.0 on Godot 3.5.2: SDL2 backend, GLES2 **and** GLES3 renderers, no WebM module) | Eight source projects ported to it; a shipped Godot 3.5.1 Steam game ran on it (with a custom build for memory, see below). |
| 4.x | A stock Godot export template of the matching version | Dome Keeper (4.3.1) reached its title screen with zero script errors on the official template, on the PC only. Small Godot 4 projects were skipped while there was no tested Godot 4 path on the Mali-G31 devices. |

## Route 1: Godot 3 projects with source

[export_pck3.sh](../../godot/) exports a Godot 3 project headless with an official Godot 3.5.2 into
a pack for `frt_3.5.2`, on a copy of the project. The settings it forces are the lessons:

- **GLES2 and ETC1 texture import.** frt's GLES2 cannot load the S3TC or ETC2-only VRAM textures 3D
  projects import by default: Wind-Up Racer's track did not load until it was ETC1.
- **The export preset has to exist**, and Godot stops reading presets at the first gap in the
  `preset.N` numbering: add the Linux/X11 preset at the next free index.
- **Include filters.** Games that read `.txt`, `.md` or `.json` at runtime need them listed in the
  preset (`PM_INCLUDE`). One More Level retried world generation and then segfaulted without its
  prefab files. Look for the upstream project's own export notes.
- **Stretch mode for fixed-size games** (`PM_STRETCH=2d` or `viewport`, aspect keep). `viewport`
  also renders fewer pixels: Holonomy at 512x512 ran ~48 fps in levels, ~36 fps at 720x720 (RG Cube
  XX, Oct 2026).
- **A drawn pointer** (`PM_CURSOR=1` adds `pm_cursor.gd`): KMSDRM has no hardware cursor, so
  mouse-driven games show nothing to aim with. It hides after a few seconds idle.
- **An FPS counter for test builds** (`PM_FPS=1`): frt ignores `--print-fps`.
- `PM_SET` overrides project settings and `PM_PATCH` applies a patch to the copy, so port changes
  never touch the upstream clone.

Launcher: copy the launcher of a merged `frt_3.5.2` port in
[PortMaster-New](https://github.com/PortsMaster/PortMaster-New). It mounts the runtime squashfs
and runs `frt_3.5.2 --main-pack <game>.pck`; PortMaster downloads the runtime on first launch
(`"runtime": ["frt_3.5.2"]` in port.json, see [packaging](../packaging.md)).

**Input: map the pad to keys.** On the RG Cube XX frt reacted only to gptokeyb2 keys, not to the
pad, so every frt port maps the pad to the game's keyboard actions (and a stick to the mouse where
needed). Even so, frt reads the pad too: Godot 3 maps it with PortMaster's SDL mapping, so in
Cassette Beasts every press arrived twice (once as a gptokeyb2 key, once as a joypad button) and
two-item menus wrapped straight back. A custom frt can switch SDL's joystick subsystem off (see the engine
patches below); with the stock runtime, map either keys or the pad, never both.

**3D on the Mali-G31**: a shadow-casting DirectionalLight cost half the frame rate in GLES2
(Wind-Up Racer: 31 vs 60 fps); shadow distance, shadow size and render resolution barely mattered.
Turn shadows off first. Larger 3D games were skipped as too heavy for this GPU.

## Route 2: shipped packs (Steam, GOG)

The pack from a Windows or Linux depot runs on a stock engine of the same version once the
platform-specific parts are dealt with. Patch the user's pack on the device at first launch; never
work on (or ship) the original.

### Things a stock engine lacks

- **Compiled-in singletons and classes** (GodotSteam's `Steam`, PlayFab): give the game GDScript
  stand-ins. Singletons become autoloads in `override.cfg` next to the binary; `class_name` classes
  are merged into the pack's class cache with `pck_patch.py --classes`. `gdc_tokens.py` shows which
  members the game calls on them. Examples in [stubs-example](../../godot/stubs-example/).
- Some games cope with the missing singleton by themselves: Cassette Beasts (Godot 3.5.1 with
  GodotSteam) ran on a stock 3.5.2 with zero script errors and no stubs.
- **Feature tags** change code paths. Use the export templates, not the editor binary (it adds the
  `editor` feature); drop a store's feature (`_custom_features` in `override.cfg`). frt advertises
  the **`mobile`** feature, so any project key with a `.mobile` variant (`quality/depth/hdr.mobile`,
  `shadow_atlas/size.mobile`) beats the plain key you write in `override.cfg`: write the `.mobile`
  key too, or your override does nothing.

### Per-GPU texture remaps (Godot 3)

A 3D or VRAM-compressed texture is imported once per GPU format, with a remap that lists
`path.s3tc=` and `path.etc2=`. A Steam pack contains only the desktop (S3TC) files. Mali has ETC2
and no S3TC, so Godot follows the `etc2` entry to a file that is not in the pack. Cassette Beasts:
13 such textures, autoloads failed in a preload cascade and the game segfaulted after ~17 s on the
RG Cube XX. The PC never showed it, because Mesa's llvmpipe has S3TC. Fix: rewrite those remaps to a
plain `path=` pointing at the S3TC file (GLES3 then decompresses it on the CPU), or better, convert
the texture to ETC2.

### GLES3 games stay on GLES3

`frt_3.5.2` contains the GLES3 renderer and it ran on the Mali-G31 (Knulli). Switching a GLES3 game to
GLES2 is not a way out: in Cassette Beasts 30 of 101 shaders failed under GLES2 (unsigned integer
hash functions, used by every character sprite shader), and Godot 3's GLES2 has only ETC1 (no
alpha), which would have cost ~180 MB more.

### The game's own settings come first

Shipped games often have a console or mobile code path already:

- Project feature overrides for `mobile` or a console (Cassette Beasts has Switch values) show what
  the developers turned off for weak hardware: scene preload and shader precompile at start, shadow
  sizes. Copying the Switch values into `override.cfg` took the PC title screen from "still loading
  after 120 s at 2.35 GB" to "title at ~45 s, 1.46 GB".
- The user settings file (`user://settings.cfg`) defaults are often desktop values: 1920x1080, MSAA,
  depth of field, glow, shadows. Seed a handheld version on first launch. With the defaults the
  title background was pink and glitchy on the Mali-G31; the handheld settings, together with
  MSAA/HDR off in `override.cfg`, used ~70 MB less GPU memory.
- Look for streaming or quality settings in the project: Cassette Beasts' level streamer
  pre-instanced every chunk of the map half (`instance_chunks_when=1`); the in-range mode (`2`) via
  `override.cfg` took the Overworld from 714 to 461 MB of Godot's static memory.
- A game setting can overwrite an engine setting: Cassette Beasts' `framerate` option sets both
  `Engine.target_fps` and the physics rate, so `physics_fps` in `override.cfg` had no effect.

## Memory: the main problem on 1 GB devices

Both shipped games the author worked on started far above what a 1 GB handheld offers (the RG Cube
XX has 972 MB and gives a game ~700 MB with the front end running): Dome Keeper 1.63 GB RSS at the
title on the PC; Cassette Beasts ~805 MB at the title and ~895 MB entering a new game, on the device
with temporary zram. A custom frt build now reaches the Overworld in 1 GB (~29 fps capped at 30);
that port is not finished ([engine changes](#engine-changes-a-custom-frt-build)). Measure before
changing anything.

### Measuring

- `pck_assets.py` lists every asset with its imported type and size, biggest first (Dome Keeper:
  708 MB of 16-bit PCM samples, 313 MB of preloaded music).
- [MemProbe.gd](../../godot/probe/) as a test-only autoload writes Godot's memory monitors and every
  cached asset with an estimated size at fixed game times. `run_pck.sh` runs a pack in a private
  Xvfb with an RSS log and screenshots.
- On Mali devices **RSS = CPU heap (RssAnon) + GPU memory** (the driver's mappings count as
  RssFile). `/sys/kernel/debug/mali0/gpu_memory` gives the GPU total per context. Framebuffer
  screenshots are blank (the GPU draws on its own plane).
- Godot's `static_mem` monitors are 0 in release builds (frt); use a `release_debug` build of the
  same version on the PC for them.
- llvmpipe on the PC keeps CPU copies of textures, compiles shaders into large JIT code and cannot
  sample ETC2 in Godot 3's desktop GL: PC RSS is an upper bound with a different shape. Judge
  texture memory on the device. More: [performance and memory](../performance-and-memory.md).

### Conversions on the device (the user's pack, first launch)

| What | Change | Cassette Beasts |
|--|--|--|
| 16-bit PCM samples | IMA ADPCM (Godot 3 AudioStreamSample format 2) | 1702 samples, 357 -> 89 MB |
| Lossy WebP and DXT textures in illustration folders | ETC2 (etcpak with heuristics on: off gave 24 dB instead of 31), only where >= 30 dB | 294 textures, 540 -> 125 MB as RGBA8 |
| Grey textures | L8 / LA8 (tolerance 2: lossy WebP greys decode with R, G, B a step apart) | anywhere |
| Preloaded music (Godot 4) | `lazy_music.py`: each music import repointed to a stream that loads on first play | Dome Keeper title: 1.63 -> 1.10 GB RSS |

On-device conversion took about a minute each for samples and textures on the RG Cube XX (SD-card
bound). Keep palette-swap art lossless: shaders that match exact colours (Cassette Beasts swaps with
a distance below 1/256) break with any lossy format. Godot 3 has no ASTC at all.

### Engine changes (a custom frt build)

When settings and conversions are not enough, the remaining lever is the engine. For Cassette
Beasts the author rebuilt `frt_3.5.2` from source (FRT 2.1.0 + Godot 3.5.2, clang against a bullseye
arm64 sysroot: see [building native code](../building-native-code.md)) with patches that are all
off by default and switched on in `override.cfg`. The patch set is not published; the techniques:

- **Lazy textures**: StreamTextures read only their header at load; the renderer decodes and uploads
  on first bind and drops textures not drawn for N seconds. PC title: texture memory 383 -> 19 MB.
- **Lazy audio**: Ogg data and samples stay in the pack and are read when a playback starts.
- **3D render scale**: the 3D scene renders into a smaller target and is scaled up, the 2D UI stays
  at full resolution and sharp. A pixel cap does the same for the game's own large 3D viewports.
- **Streamed font files** (FreeType reads through a file stream instead of a power-of-two buffer: an
  8.5 MB font took 16 MB) and **translations loaded per language**.
- **Render target clamp**: HDR, MSAA and 3D effect buffers off at allocation time, including the
  root viewport.
- **Start-up buffers the game cannot use** (reflection and shadow cubemap ladders, ~25 MB) skipped when
  the atlas sizes are 0.

Result on the RG Cube XX (Knulli, Mali-G31, Oct 2026): the game reached its Overworld in 1 GB without
swap (title at 60 s instead of 120 s); with the final configuration 29.1 fps average at a 30 fps
cap, 2.2 % slow frames, Mali context 110 MB (was 167), 326 MB still available in the Overworld.
Battles passed in unattended runs with only 70-95 MB to spare. The port was not finished
(packaging, name entry needing an on-screen keyboard, boss fights unmeasured).

Measured dead ends, worth knowing before you try them:

- **Evicting textures did not return GPU memory**: the Mali blob keeps freed memory and the GPU
  total tracks the peak. Evicting shader variants freed nothing measurable and under pressure caused
  a recompile spiral (one frame per 7-10 s).
- glibc arenas held only 10-40 MB free; `malloc_trim` gained nothing.
- Pressure-driven eviction in a battle helps little: almost every resident texture is on screen.

### Engine bugs found on the way (frt 2.1.0 / Godot 3.5.2 on Mali)

Fixed in the custom build; check for them if a shipped Godot 3 game misbehaves on `frt_3.5.2`:

- frt creates the visual server without its multithreaded wrapper, so calls from loading threads run
  GL without a context: incomplete framebuffers, a black instead of white loading fade, crashes.
- Mali returns NULL for `glMapBufferRange` reads: scripts that read mesh arrays get empty arrays,
  then a segfault. Keep CPU copies of mesh data.
- With the 3D effect buffers off, GLES3 never binds `DEPTH_TEXTURE` / `SCREEN_TEXTURE`: shaders that
  read them see garbage (characters drawn as dark silhouettes).
- Materials set before a mesh's surfaces arrive from a loading thread were dropped ("Index
  p_surface = 0 is out of bounds"): props drawn with the default material.
- Release builds do not validate render IDs: stale material and instance IDs from scripts wrote into
  freed memory.
- `rendering/threads/thread_model=2` broke rendering on the device (invalid framebuffers, endless
  shader errors) while the PC build was fine.

## Frame rate

- **Full-resolution 3D is fill bound on the Mali-G31**: frame rate roughly followed the pixel count
  (Cassette Beasts Overworld: 720x720 10 fps, 480x480 20 fps, 360x360 32 fps). The 3D render scale at
  0.5 gave 26-27 fps with a sharp UI.
- After that the main thread is the limit: a device profile showed the game's own GDScript
  `_process` callbacks at 37 %, the renderer at 12 %, physics at 8 %. Fewer instanced objects beats
  further renderer work.
- Capping at 30 fps through the game's own setting gave even frames (2.2 % slow frames instead of
  13 %). Pinning the GPU's minimum clock was worth ~0.2 fps at the cap.
- Godot 3's `TIME_PROCESS` monitor includes the renderer's draw, and `TIME_PHYSICS_PROCESS` is the
  slowest single step of the last second, not a per-frame sum: do not read them as script time and
  physics time per frame.

General method: [performance and memory](../performance-and-memory.md).

## Testing

- `run_pck.sh` runs a pack on a stock Godot in a private Xvfb (log, RSS per second, screenshots,
  optional `override.cfg` and probe). Without a window manager in Xvfb, xdotool keys need
  `xdotool search --class Godot windowfocus --sync key ...` to arrive.
- Things the PC hides: missing ETC2 files (llvmpipe has S3TC), ETC2 memory (Godot 3's desktop GL
  cannot decode ETC2), Mali GPU memory and the `glMapBufferRange` failure. Get to a device early.
- Unattended device runs: a test autoload that presses keys through the game's own input (or
  drives the game's debug functions) and writes a watch log once a second, plus a launcher that
  starts the run through the front end's API. Restore the tester's saves afterwards: games autosave.
- gdb exists on Knulli; build the same engine unstripped to symbolize device backtraces. See
  [testing and debugging](../testing-and-debugging.md).
