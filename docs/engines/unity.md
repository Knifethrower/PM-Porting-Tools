# Unity 5.x to 2017.3 (Linux builds)

How to run a Unity 5.x to 2017.3 game's Linux x86_64 build on a GLES-only aarch64 handheld: how the player
behaves, what has to be converted in the game data (shaders, textures, audio, timing, settings) and the
first-run converter that does it on the device. Newer Unity versions are not covered. Unity 4 is a different
pipeline: see [Unity 4](unity-4.md).

The lessons come from two ports: Night in the Woods (Unity 5.6.2p4, Mono, FMOD Studio 1.09, 2D) and Gone Home
(legacy Mono, deferred rendering + HDR, NGUI, 3D first person). Both run on the Anbernic
RG353V (RK3566, Mali-G52, 2 GB, 640x480, dArkOS); Gone Home was confirmed playing very well there.

Contents: [The route](#the-route) · [Candidate checks](#is-the-game-a-candidate) ·
[The player](#how-the-unity-linux-player-behaves) · [Converting the data](#converting-the-game-data) ·
[unityport](#unityport-the-converter-as-one-tool) · [Threads](#threads-and-the-gl-context) ·
[Performance](#performance) · [Firmwares](#status-per-firmware) · [PC testing](#testing-on-the-pc) ·
[Logs](#logs-to-ask-for) · [Controls](#controls)

## The route

| Piece | What it does | Details |
|--|--|--|
| box64 | runs the x86_64 player, Mono and the game's native plugins | [box64](../box64.md) |
| Westonpack (`crusty_glx`) | gives the X11-only player a display and a GLX context on the handheld | [graphics](../graphics.md) |
| glespass | a `libGL.so.1` that passes GL calls straight to the driver's GLES, since the converted game is already GLES | [graphics](../graphics.md), [`shim/glespass-nitw`](../../shim/glespass-nitw/) |
| first-run conversion | converts the user's copy of the game on the device (shaders, textures, audio, settings) | [unityport](../../unity/unityport/), below |

Other routes that were tried or estimated (re-rendering assets, console ports, native ARM Mono) and the
author's notes on Android builds are in [choosing an approach](../choosing-an-approach.md).

## Is the game a candidate?

- **A Linux x86_64 build exists** (GOG, Steam). A Windows-only Unity 5.x to 2017.3 game rarely carries OpenGL shaders
  (Unity 4 Windows builds usually do, see [Unity 4](unity-4.md)), and a Windows-only IL2CPP game would need Wine.
- **Scripting backend**: Mono (`<Game>_Data/Mono/x86_64/libmono.so` = legacy Mono 2.x, or `MonoBleedingEdge/`)
  or IL2CPP (`GameAssembly.so`). Both run under box64; Mono has a JIT, which matters for box64 settings
  ([box64](../box64.md)).
- **Unity version**: in the first bytes of `<Game>_Data/globalgamemanagers` (e.g. `5.6.2p4`). The shader blob
  format and the serialized layouts depend on it.
- **RAM**: Night in the Woods had a resident size of ~840 MB on the RG353V. 1 GB devices (R36S, RG351) are
  marginal, so these ports use the `"2gb"` requirement in `port.json`.
- `python -m unityport survey <game folder>` on the PC reports most of this in seconds (below).

## How the Unity Linux player behaves

- The player is **X11-only** and embeds **SDL2**; it gets GL through SDL and `glXGetProcAddressARB`.
- It asks GLX for a **4.5 core** context first (`glXCreateContextAttribsARB` with 0x2091=4, 0x2092=5,
  0x9126=1), and creates and destroys a probe context before the real one.
- **Graphics API list**: `BuildSettings.m_GraphicsAPIs` in `globalgamemanagers` (GraphicsDeviceType: 8 = GLES2,
  **11 = GLES3**, 17 = GLCore, 21 = Vulkan). Set to **[11, 17]**, the player uses its **GLES3 renderer** even
  on Linux. It logs "Unknown renderer 11" but works when the GLX context it gets is really GLES (crusty
  provides that).
- Useful player flags: `-force-gfx-st` (render on the main thread), `-force-glcore` (also `-force-glcore44/45`),
  `-force-opengl`, `-screen-fullscreen 1 -screen-width W -screen-height H`, `-logFile path`.
- **`-logFile` redirects stderr into the log file**, and Unity's own writes overwrite lines from anything else
  writing to stderr. Send diagnostics from injected libraries to their own file (`open(O_APPEND)` and one
  `write()` per line).
- The player finds its data folder from the **executable name** (`<name>_Data`). Westonpack re-evaluates the
  command as a string, so **rename both to names without spaces** (`NITW.x86_64`, `NITW_Data`).
- **Threaded rendering** (the default) runs GL on a separate render thread. It matters a lot for speed and
  breaks under crusty unless the GL context is really handed to that thread (see
  [Threads and the GL context](#threads-and-the-gl-context)).
- Harmless errors in every run: `Hidden/VideoDecodeOSX`, `NoiseAndGrainDX11` and other DX11- or macOS-only
  shaders failing, `libudev.so.0` not found (Rewired), Mono GC signals 30/24 (shown only with
  `BOX64_SHOWSEGV=1`).

## Converting the game data

Nothing from the game may be shipped, converted or not, so the conversion runs on the device on the first
launch, from the user's own copy, through PortMaster's patcher ([packaging](../packaging.md)). All the steps
below are in [unityport](../../unity/unityport/); the single scripts it grew from are in
[`unity/converters/unity5-unitypy/`](../../unity/converters/unity5-unitypy/). They use
[UnityPy](https://github.com/K0lb3/UnityPy) 1.25.3 (`UnityPy.load(file)`, `obj.read_typetree()`,
`obj.save_typetree(tree)`, `serialized_file.save()`).

### Shaders: GLCore -> GLES3

This is the essential step for Mali's GLES-only drivers.

- Unity Linux builds ship only **GLCore** shaders (platform 15) and sometimes Vulkan. The Mali blob driver is
  GLES only.
- Each `Shader` has `platforms`, `offsets`, `compressedLengths`, `decompressedLengths` and one **LZ4
  block-compressed** `compressedBlob`. The per-platform blob (format from 5.5 on) is
  `int count; {int offset, int length}[count]`, and each entry is `version, gpuProgramType, stats (12 bytes),
  a 5.5+ field, keyword list, int codeLen, code (4-aligned), tail`.
- Conversion: GLSL `#version 150` / `330` -> `#version 300 es` plus `precision highp float/int` (and sampler
  precisions); drop the `GL_ARB_explicit_attrib_location` / `GL_ARB_shader_bit_encoding` extension lines; strip
  `noperspective`. Programs are split on `#ifdef VERTEX` / `#ifdef FRAGMENT`. `#version 4xx` programs
  (DX11-only effects) are left alone.
- Append the converted blob as platform **9 (GLES3Plus)**, GPU program type **4 (GLES3)**, and give every
  GLCore `SerializedSubProgram` (types 6/7/8) a GLES3 twin in
  `m_ParsedForm.m_SubShaders[].m_Passes[].prog{Vertex,Fragment,...}.m_SubPrograms`.
- Keep the GLCore variants: a desktop-GL path (Panfrost on ROCKNIX) needs them.
- Validate with `glslangValidator` on the PC; on the device, log compile and link failures from the GL shim.
- The same blob format and conversion worked unchanged for Gone Home's 93 shaders (2,010 stages) all
  pass glslangValidator.
- Then set the graphics API list to `[11, 17]` (see the player section above).

### Textures: DXT -> ASTC

Mali has no S3TC (DXT), so DXT textures are decoded and re-encoded as ASTC.

- Decode with UnityPy (texture2ddecoder), resize with Pillow LANCZOS, encode with astc-encoder-py
  (`ASTCContext`, quality "fast").
- Unity stores images **bottom-up**: flip before encoding. Pick RGB or RGBA ASTC formats by the source's alpha.
  Write `m_CompleteImageSize` and `m_StreamData {offset, size, path}` (offsets **16-byte aligned**) and clear the
  inline `image data`.
- **Choose rules per kind of game.**
  - 2D (Night in the Woods): half resolution at ASTC 6x6; text and lettering textures (names matching
    `dialogue_|text(?!ure)|letter|font|glyph`; plain `text` would also match "Texture") at full resolution and
    ASTC 4x4; textures of 128 px or less, or with a side under 32 px after scaling, keep their size; skip
    readable textures and LUTs (`w == h*h`). `m_MipCount = 1` is fine for 2D.
  - 3D (Gone Home): **only downscale world textures.** NGUI (and similar UI kits) store sprite rectangles in
    *pixels* in MonoBehaviours, so a resized atlas breaks the UI. Rule used: half size + ASTC 6x6 only for
    mipmapped textures in `sharedassets*` whose names are not text-like; everything else (`resources.assets`,
    textures without mips, notes, letters, labels) keeps its size at ASTC 4x4 (5x5 for DXT1 sources).
- **Regenerate mip chains for 3D games**: encode each level separately, largest first, each flipped bottom-up;
  `m_MipCount = max(w,h).bit_length()`, `m_CompleteImageSize` = the sum of all levels.
- **`QualitySettings` `textureQuality`** (master texture limit) drops 1 or 2 mip levels of every mipmapped
  texture on the low quality levels. On top of a half-size conversion that made Gone Home's notes unreadable:
  set it to 0 on every level.
- DXT5nm normal maps: keep RGBA (x in A, y in G); the GLES shaders converted from GLCore still unpack DXT5nm.
- Inline (non-streamed) DXT leftovers are fine: Unity decompresses unsupported formats on load.
- **Sprites** that use a scaled texture need their pixel-space fields scaled: `m_Rect`, `m_Offset`, `m_Border`,
  `m_PixelsToUnits`, `m_RD.textureRect`, `textureRectOffset`, `atlasRectOffset` (unless -1), `uvTransform`.
  Sprites whose texture lives in **another assets file** (`m_FileID != 0` -> `externals[FileID-1]`) are fixed in
  a second pass from the original and new sizes recorded during the texture pass.
- Results: Night in the Woods 5.8 GB -> 2.0 GB in total (textures 755 MB); Gone Home textures 1,108 -> 447 MB.
- The ARM NEON and x86 builds of astcenc do not give bit-identical output (PSNR >= 56 dB between them). That is
  expected; compare by PSNR, not by hash.
- On Windows, UnityPy keeps files open: drop all object references and `gc.collect()` before `os.replace()`.

### Audio: PCM -> IMA ADPCM, and streaming

- Gone Home shipped all its audio as 16-bit PCM (1.8 GB). FSB5 codec 7 (IMA ADPCM) is played natively by Unity's
  built-in FMOD (`m_CompressionFormat = 2`, Unity's "ADPCM"). Re-encoding is ~3.5x smaller (1.8 -> 0.5 GB) and
  cheap to decode.
- Layout (Xbox IMA): per channel and block a 4-byte header (int16 first sample, int8 step index, 0), then 32
  bytes of nibbles (low nibble first) for samples 1 to 63; the 64th nibble is padding, so 64 samples take 36
  bytes. Stereo: 72-byte blocks, both headers first, then alternating 4-byte chunks. Header: copy the original
  FSB5 (base 0x3C for version 1), set the codec at 0x18 and the data size at 0x14, pad the data to 32 bytes;
  sample headers stay (the sample count does not change).
- The encoder is a small freestanding C library (no libc, so it runs on any glibc) called through ctypes. Pure
  Python would take hours on the device.
- Verify with an independent decoder (vgmstream-cli: exact sample counts, SNR ~35 dB) and by recording Unity's
  real output on the PC: a tone at the block rate (sample rate / 64, ~689 Hz) means a layout mismatch.
- **Long clips held in RAM** (`m_LoadType` 0 or 1, 8 MB or more) switched to Streaming (2) save up to 250 MB
  each.

### FMOD Studio (Unity integration)

- FMOD 1.09 asserts or exhausts its memory pool when ALSA hands it odd period sizes. dArkOS's default device
  (plug -> softvol -> dmix at 44.1 kHz, 1024 frames) resampled FMOD's 48 kHz and gave 1114-frame periods.
  Opening `hw:` directly fails there (EmulationStation and fluidsynth hold the card).
- Fix: set the integration's **SampleRateSettings Default = 44100** in the `FMODStudioSettings` MonoBehaviour in
  `resources.assets` ([`fmod_samplerate.py`](../../unity/converters/unity5-unitypy/fmod_samplerate.py); the list is
  found by pattern next to SpeakerModeSettings).
- Banks live in `StreamingAssets/*.bank` (RIFF with an FSB5 inside). Night in the Woods' `Master.bank` is
  1.04 GB: 9.6 h of Vorbis at ~241 kbps, 48 kHz. Re-encoding could save ~600 MB and CPU, but FSB5 Vorbis strips
  the setup headers and refers to FMOD's **built-in table by CRC32**, so re-encoded audio would have to match a
  table entry. Not tried.
- FMOD used about one core in Night in the Woods (37% of all CPU). It does not limit the frame rate directly
  but adds heat and throttling.
- `StreamingAssets/PS4/` (videos) is console-only; nothing references it on Linux.
- Firmware audio stacks are covered in [devices and firmwares](../devices-and-firmwares.md).

### Game time vs frame rate (check this in every port)

- **`TimeManager` `Maximum Allowed Timestep`** (in `globalgamemanagers`) caps how much game time one frame may
  advance. Gone Home had 0.0333: below 30 fps the whole game ran in slow motion while audio played in real time,
  so **subtitles lagged the audio** (worst in the intro) and walking was slow.
- Fix: `Maximum Allowed Timestep = 0.2` (in sync down to 5 fps) and `Fixed Timestep = 1/30` (was 1/60), so a slow
  frame does not run more physics steps than before (CPU time is short under box64). Confirmed on the device.
- Symptom to recognise: audio or subtitle drift, cutscenes or movement slower than real time at low fps.

### Game options files override the command line

- Gone Home keeps resolution and quality in its own `Options.sav` (.NET BinaryFormatter) and ignores
  `-screen-width/height`. The port parses the file, ships a low-end template and patches the resolution and
  aspect ratio at fixed offsets on first run
  ([`options_sav_gonehome.py`](../../unity/converters/unity5-unitypy/options_sav_gonehome.py)).
- With fullscreen on, Unity only accepts listed display modes: on the PC (WSLg) 640x480 fell back to 1600x900.
  That is fine on the device, where 640x480 is native. Test windowed on the PC.

## unityport: the converter as one tool

[unityport](../../unity/unityport/) is the first-run converter of both ports as one reusable tool. A new
Unity 5.x to 2017.3 port gets a `game.toml` instead of a copied install script.

- `python -m unityport survey <game folder>` reports the Unity version, backend, graphics APIs, shaders without
  GLES3 programs, textures by format, PCM clips and the maximum timestep, and prints a starting config.
- `python -m unityport install game.toml <game folder>` runs the steps (`shaders`, `settings`, `textures`,
  `sprites`, `audio`, `fmod`, `timing`) in order. Each rewritten file goes through `<file>.tmp` and a journaled
  rename, so a setup that is killed (patcher closed, battery flat) resumes where it stopped.
- With the two example configs it produces byte-identical game folders to each port's own installer, from the
  GOG installers.
- Bump the setup version in the config after adding a step: the launcher re-runs setup when
  `gamedata/.installed` differs, and only the new steps run. That is how Gone Home's timing fix reached
  existing installs.
- Its README has the full config reference and the device-side command line. As of Oct 2026 the package
  layout itself has not been run on a device.

Times: Gone Home's conversion takes ~1 to 1.5 min on a PC and an estimated 20 to 40 min on the device (not yet
timed); Night in the Woods' ~6 min on a PC. For your own test devices, converting on the PC and copying the
result saves time.

Python on the device (PortMaster's `python_3.11` runtime, which wheels to vendor, the UnityPy accelerator that
crashed) is covered in [packaging](../packaging.md).

## Threads and the GL context

Unity 5+ renders on a separate render thread by default, and crusty does not support that out of the box. This
section owns the fix; glespass itself (how it binds GL calls, its diagnostics) is on
[graphics](../graphics.md#glespass-gles-games-on-crusty-without-gl4es).

- **The problem**: crusty's `glXMakeCurrent` is bookkeeping only. Its single EGL context stays current on the
  main thread forever. Unity's render thread then "binds" it and draws into nothing: shaders get id 0, 224 link
  failures, black screen.
- **First workaround**: `-force-gfx-st` (rendering on the main thread). It works but is slow.
- **The fix** (glespass, `GLESPASS_CTXFIX=1`): wrap `glXMakeCurrent` / `glXMakeContextCurrent` and hand the
  context over for real with `SDL_GL_MakeCurrent(crusty's window, crusty's SDL context)`. Window and context are
  captured with `SDL_GL_GetCurrentWindow` / `SDL_GL_GetCurrentContext` on the main thread; SDL is found through
  `CRUSTY_LIBSDL`.
- A raw `eglMakeCurrent` is not enough: `SDL_GL_SwapWindow` refuses a window SDL has not made current on the
  calling thread (SDL keeps that state per thread), which gives a black screen although frames are rendered.
- The handoff logic was developed against fakes of GLES, EGL, SDL and crusty (per-thread current context and
  window), so it was tested on the PC before the device.
- Result on the RG353V (dArkOS), Night in the Woods, Sep 2026: median fps during play 11 -> 16.5, Mae Street
  ~12 -> ~22-25.
- Check the log: `GfxDevice: creating device client; threaded=` in `unity.log` says whether the render thread
  is on.

## Performance

Under box64 these games are **CPU-bound on Unity's main thread** (Mono scripts + engine); the GPU and the render
thread have headroom. Turning effects off first seemed to change nothing, although a later standing-still A/B on
the RG353V with FXAA, Bloom and NoiseAndGrain off went from 19.5 to 25 fps. What helped on the RK3566: box64's faster
settings, threaded rendering (above), the performance governor and pinning the main and render threads to
their own cores, away from FMOD's real-time mixer threads. The method, the box64 profiling results and the
pinning code are on [performance and memory](../performance-and-memory.md) and [box64](../box64.md).

Night in the Woods on the RG353V (threaded rendering, pinned): title 60 fps, rooms 25-33, Mae Street 22-25,
Underhill 15-18, median during play 16.5. The SoC throttles from 1992 to 1608 MHz after about a minute.

## Status per firmware

As of Sep 2026, Unity 5.6 (Night in the Woods) on Westonpack with crusty + glespass. Westonpack picks its mode per
firmware: crusty on dArkOS and ArkOS; on ROCKNIX it runs `glxinfo`, and if that reports a desktop GL version
(Panfrost, Mesa) it bypasses Weston and crusty and runs the command directly on ROCKNIX's desktop
(`NO_PANFROST_BYPASS=1` turns that off), otherwise (libmali) it runs crusty in Wayland mode.

| Firmware and driver | Result |
|--|--|
| dArkOS on RK3566 (RG353V, libmali) | works: crusty + glespass, GLES3 renderer, threaded rendering. ArkOS not tested separately |
| ROCKNIX + libmali (crusty in Wayland mode) | the game runs but every frame is pure black: a readback of the default framebuffer gives 0,0,0,255 at every sampled frame; shaders, GPU features and extensions are the same as on the working device. Unresolved |
| ROCKNIX + Panfrost | westonwrap's bypass runs the game on native Mesa GL, with `-force-glcore` and `MESA_GL_VERSION_OVERRIDE=4.5 MESA_GLSL_VERSION_OVERRIDE=450` (Panfrost reports GL 3.1; Unity requires 3.2 core or newer and asks for 4.5). It needs the GLCore shader variants the conversion keeps. Written, not confirmed on a device, and removed from the release. Watch ASTC support under desktop GL |

The Night in the Woods port therefore targets only the RG353V on dArkOS. In the launcher log, westonwrap's mode
line ("Rocknix (Mali) detected" or "bypassing weston setup") tells which row applies (see
[Logs to ask for](#logs-to-ask-for)). Unity 4 uses a different route with its own status:
[Unity 4](unity-4.md#the-runtime-no-westonpack).

## Testing on the PC

- Mesa llvmpipe offers GLES 3.2 GLX contexts and ASTC. An x86_64 `LD_PRELOAD` that hooks `dlsym` /
  `glXGetProcAddressARB` and rewrites the `glXCreateContextAttribsARB` attributes to ES 3.2 (`0x9126 = 0x4`) runs
  the converted game natively on the PC: the log says "Creating OpenGL ES 3.2 graphics device". See
  [`gles_force.c` and `run_gles.sh`](../../devtools/pc-testing/). `LD_PRELOAD` paths cannot contain spaces: copy
  the library to `/tmp` first.
- Driving the game with xdotool at low fps: hold mouse buttons ~0.8 s; `mousemove_relative` works for mouse-look.
- Test the converter end to end against a fresh copy of the original files, compare with a known-good build by
  hash, then kill it mid-run and resume. Run one assets file through the ARM Python stack under qemu and compare
  images by PSNR.
- The rest (qemu-user, Xvfb, a fake PortMaster patcher) is on [testing and debugging](../testing-and-debugging.md).

## Logs to ask for

- `unity.log` (the player's `-logFile`): `GfxDevice: creating device client; threaded=`, `Renderer:` /
  `Version:` and the extension list, `Creating OpenGL ES 3.2 graphics device`, shader failures, scene names
  (`Init<Scene>` lines), FMOD errors.
- `log.txt` (launcher, westonwrap, box64): the firmware and device line, westonwrap's mode ("Rocknix (Mali)
  detected" or "bypassing weston setup"), crusty's SDL attributes, the glespass binding summary
  (`431/1222 gl, 40/40 glX bound`), box64 crashes.

## Controls

- A gptokeyb keyboard + mouse mapping (right stick = mouse) works for first-person Unity games.
- Map Enter to a button (Select) for menus that use Unity's Submit action.
