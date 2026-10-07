# macOS games through machismo

Games whose Mac build has an Apple Silicon (arm64) slice can run natively on the handhelds through
[machismo](https://github.com/bmdhacks/machismo), a Mach-O loader for aarch64 Linux that maps the
game's dylib imports to Linux libraries. This page covers checking a Mac build, what broke on a
Cortex-A53 device, and the engine-level fixes. The tools are in [machismo/](../../machismo/);
their READMEs are not repeated here.

Evidence: the author's ports (not yet submitted to PortMaster, Oct 2026) of two PUNKCAKE Délicieux
games on the Sugar engine (LuaJIT, freetype, OpenGL), Spaghetti Celesti (Sugar with SDL3) and Tummy
Bonbons: The Sweet Monster (Sugar with SDL2), both playing on the RG Cube XX (Allwinner H700, 4 x Cortex-A53, Mali-G31,
Knulli, 720x720). PortMaster's Crypt of the NecroDancer, Mina the Hollower, Shotgun King, The
Wratch's Den and Scavenger of Dunomini ports use the same loader.

## When this route applies

- The Mac build must have an **arm64** slice (universal or arm64-only). x86_64-only Mac builds are
  a [box64](../box64.md) job, if anything.
- Engines with a better route (Unity, LÖVE, Godot, GameMaker, FNA, Java and others) go that way;
  see [choosing an approach](../choosing-an-approach.md).
- Blocked: FairPlay-encrypted builds (Mac App Store, Apple Arcade), the Swift runtime, Metal-only
  renderers. Hard: own Cocoa window and input code, heavy Objective-C.
- Best case: SDL2 or SDL3 for the platform layer, OpenGL or bgfx for drawing, few Apple framework
  calls, symbols not stripped (both Sugar games had theirs).

**Survey first.** [macsurvey](../../machismo/macsurvey/) reads a `.app`, folder, zip, `.pkg` or
tarball in memory and prints a verdict, the engine, platform layer, renderer, Objective-C use,
cautions (Steam init, FMOD, AppKit calls) and the executable's MD5. With `--conf` it writes a
draft `dylib_map.conf` and `machismo.conf` to start from. Spaghetti Celesti came out as
`CANDIDATE`: arm64, SDL3, OpenGL, no Objective-C; Tummy Bonbons as `CANDIDATE` with the same
dylib set as The Wratch's Den.

## Start from a port of the same engine

machismo ports are engine-specific: the `dylib_map.conf`, trampolines, override libraries and
instruction patches of a merged port of the same engine are the best start. Spaghetti Celesti
started from Shotgun King (Sugar, SDL3), Tummy Bonbons from The Wratch's Den (Sugar, SDL2, the same
dylib set).

- **Clone reference ports with `core.autocrlf=false`.** A CRLF `dylib_map.conf` made every mapped
  path fail with ENOENT.
- **Check what each of the reference port's patches is for** before copying it. Shotgun King's
  address patches fix mono music buffers and frame pacing; Spaghetti Celesti's music is all stereo
  44.1 kHz and it times everything with the frame delta, so neither was needed.
- **Patches by symbol survive versions, patches by address do not.** Shotgun King's Steam-init
  patch is anchored on a symbol (`at sym:__ZN5sugar5steam10init_steamEj`) with the expected
  instruction words, and matched Spaghetti Celesti's binary word for word. Address-based patch sets
  had to be re-located per game (by disassembly and a fingerprint of the engine's functions).
- **Pin the executable**: patches are written for one build, so the launcher checks the
  executable's MD5 (`KNOWN_MD5`) against the build the patches were made for.

## Cortex-A53: instructions the device cannot run

Apple Silicon has ARMv8.1+ extensions, and Mac arm64 code (compiler output and hand-written code
such as LuaJIT's) uses them. The A53 and A35 in most handhelds are ARMv8.0: LSE atomics and SHA3
instructions (`EOR3`, `BCAX`) raise SIGILL. machismo's `isa_emul` rewrites the ones it knows.

- Spaghetti Celesti died with SIGILL on the RG Cube XX (1 Oct 2026): `eor3` in the game's own
  LuaJIT (`lj_prng_u64`, called at engine start). machismo emulated LSE and `bcax` but not `eor3`;
  [machismo/patches/](../../machismo/patches/) adds it.
- **Scan every binary before the device does**: `scan_isa.sh` lists the ARMv8.1+ instructions in a
  game (Spaghetti Celesti: 17 LSE, 4 `eor3`, 1 `bcax`).
- **qemu hides this**: its default CPU has every extension, so PC runs passed. Test with
  `-cpu cortex-a53` (or `QEMU_CPU=cortex-a53`) and qemu raises SIGILL like the device.
- Shotgun King never hit it because its port sends LuaJIT to a Linux build (see below).
- Portability of native code in general (`-march`, LSE) is on
  [building native code](../building-native-code.md).

## LuaJIT: keep the game's or trampoline it

machismo can redirect ("trampoline") a statically linked library such as LuaJIT to a Linux build.
The two Sugar games needed opposite choices:

- **Spaghetti Celesti: do not trampoline.** Its Lua uses the developer's parser additions
  (`v-=DT`, `ctrl?.()`); stock LuaJIT stops at `'=' expected near '-'` and the screen stays black.
  The game's own LuaJIT (with `eor3` emulated) runs it.
- **Tummy Bonbons: trampoline.** Its built-in LuaJIT panics under machismo right after the intro
  loads its sounds (`PANIC: unprotected error in call to Lua API (8)`). Its Lua is stock syntax,
  so a LuaJIT 2.1 Linux build runs it.

Try the game's own first; if it panics, check whether the scripts parse with stock LuaJIT.

## Crashes found on the device

- **Unterminated `__eh_frame`** (Tummy Bonbons, first RG Cube XX launch, Oct 2026): machismo
  registered the game's native `.eh_frame` with `__register_frame`, but the Mach-O section has no
  zero terminator (here it ends exactly at the end of `__TEXT`, with the GOT after it). GCC 13+
  libgcc parses registered tables eagerly and ran off the end. On glibc 2.35+ machismo's
  `_dl_find_object` hook already serves those frames, so the patch in
  [machismo/patches/](../../machismo/patches/) registers the section only where the hook is
  missing. Spaghetti Celesti had run without the patch, but what follows the section (GOT entries
  filled with randomised addresses) decides whether libgcc crashes, so any launch could fail.
- **Run the binary by its absolute path.** Tummy Bonbons crashed after ~45 s in the card draft
  (`attempt to index local '_c' (a nil value)`). The engine copies its value tables to the
  preferences folder on first use and builds the source path from `argv[0]`; a relative `argv[0]`
  doubled up after machismo's chdir into `Contents/MacOS`, so it wrote **empty** copies and kept
  using them. Launch with `"$GAMEDIR/$BINARY"`, and remove the empty copies a broken launcher
  left behind. After the fix 50 drafts in a row ran clean. Found with `QEMU_STRACE=1` under qemu
  and a Lua log probe.
- **Engine bug in a shader error path**: Sugar's shader function calls
  `glGetProgramInfoLog(prog, 512, NULL, buf)` after a failed link and then copies a length it never
  received (SIGSEGV in memcpy). The compile-log calls pass a length pointer; a one-instruction patch
  does the same for the link. Spaghetti Celesti's binary has the same bug but never compiles a
  shader on the device, so it was left alone. When one game of an engine shows a bug, check its
  siblings by disassembly.

## Graphics

- The Mali GLES driver rejects the Sugar engine's GLSL 150/130 shaders. Leave OpenGL unmapped
  (machismo's system shim tries `libGL.so.1`, then `libGLESv2.so.2`) and pass the game's
  `shaderless` argument: the engine falls back to SDL rendering after its shaders fail, as designed.
- Engines whose renderer is bgfx can use machismo's GLES bgfx trampoline; desktop OpenGL games may
  need gl4es (see [graphics](../graphics.md)).

## Input

- **Window focus**: Tummy Bonbons reads keyboard and controller only while `SDL_GetWindowFlags` has
  `SDL_WINDOW_INPUT_FOCUS`, otherwise only the mouse. The handhelds have no window manager and the
  window is re-created after the shader fallback, so a patch makes it always read input. Through
  the SDL3-on-SDL2 shim (Spaghetti Celesti) the window has focus and no patch was needed.
- **Native pad or keys**: Spaghetti Celesti reads the controller natively (gptokeyb2 only gives the
  quit hotkey). Tummy Bonbons opened the controller on the firmware's SDL2 but ignored every
  button, so gptokeyb2 sends keys, as in The Wratch's Den.
- **Take the key bindings from the game's scripts**, not from guesses. Tummy Bonbons' `controls`
  block binds confirm on the game-over name entry to Return only, and `z` to up (AZERTY); a first
  mapping that sent `x` and `z` typed into the name box and moved up instead.

## Engine override libraries

A machismo port can preload a library that replaces engine functions. The Sugar SDL2 one used by
The Wratch's Den also works for Tummy Bonbons: MP3 music is decoded once into a PCM cache next to
the game (one track is 16 MB decoded, so plan for the space), and a pixel flip is replaced by a NEON
version. Spaghetti Celesti (SDL3) uses the SDL3 variant.

## Building

- machismo, its system shim and an Apple-ABI libc++ (bmdhacks' llvm-project `darwin-abi-compat`
  branch) were cross-built with clang 18 + lld against the Debian bullseye arm64 sysroot (binaries
  must need glibc 2.29 at most); see [building native code](../building-native-code.md).
- Both Sugar ports ship the same machismo build; when it gets a fix, rebuild every port zip that
  carries it (the `eh_frame` patch went into both).
- Both patches are GPLv3 like machismo and are upstream candidates.
- SDL3 games on SDL2 firmwares need an SDL3-on-SDL2 shim (as in OpenLoco, see
  [native ports](native.md#sdl3-only-upstreams)).

## Testing

- Run the release under qemu-aarch64 in the bullseye chroot on Xvfb, with the game added and
  fullscreen seeded in its settings file (Spaghetti Celesti opens a 3x window, 1152x648, otherwise).
- On Xvfb, a Sugar SDL2 game needs `xdotool windowfocus` before keys reach it (the focus check
  above).
- `QEMU_STRACE=1` traces dylib and library opens and file access; `stdio_trace.c`
  (LD_PRELOAD) logs what a game reads and in which order, for any game.
- Also run on a newer glibc (the host's Ubuntu aarch64 runtime, glibc 2.39) to catch differences in
  libgcc and the dynamic loader: the `eh_frame` crash depends on them. qemu maps memory differently
  from the device, so the unpatched build did not crash there; the patch was checked to still
  reach the game.
- Wrap WSL commands in script files: inline `wsl bash -c` loses quoting.

## Results

The author's ports, not yet submitted to PortMaster (Oct 2026):

| Game | Device result | Open |
|--|--|--|
| Spaghetti Celesti (Sugar 0.0.8f, SDL3) | RG Cube XX, Knulli: runs after the `eor3` fix (Oct 2026) | other devices, frame rate, memory |
| Tummy Bonbons (Sugar, SDL2) | RG Cube XX, Knulli, 2 Oct 2026: plays (buttons, card draft, game over and leaderboard entry), ALSA audio, saves load | other devices (RG353V, ROCKNIX on Panfrost), frame rate |

Harmless lines in Tummy Bonbons' log: "Could not read save at: ./settings.txt resorting to backup"
(the same on a Mac), "Surface ... already exists" on restart, "Could not get device name" (ALSA).
The quit hotkey kills the game, but it saves during play, so nothing is lost.
