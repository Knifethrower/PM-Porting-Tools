# Unity 4

How Unity 4 games run on aarch64 handhelds: the runtime (box64, gl4es, an X11 stand-in and GLX on the
firmware's SDL2), the fixes every Unity 4 player needs, screen shapes, input, Steam builds, patching managed
code, textures, performance and the logs to read. Games with only a Windows, Mac or 32-bit Linux build are on
[Unity 4 donor ports](unity-4-donor.md); Unity 5.x to 2017.3 is on [Unity](unity.md).

Evidence comes from four ports: Ittle Dew (GOG Linux, Unity 4.7.1f1, and its Steam build, 4.3.4f1), Teslagrad
(Steam Linux, 4.7.2f1, game code in Boo), Usagi Yojimbo (Windows only, 4.5.3f3) and Thomas Was Alone
(Steam, Windows 4.5.1 and 32-bit Linux 4.3.1), plus test runs of Ascendant (4.5.2).

Contents: [How Unity 4 differs](#how-unity-4-differs-from-unity-5) ·
[Candidate checks](#is-the-game-a-candidate) · [Runtime](#the-runtime-no-westonpack) ·
[gl4es](#gl4es-and-unity-4) · [Player fixes](#player-fixes) · [Screen shapes](#screen-shapes-169-on-every-screen) ·
[Input](#input) · [Steam builds](#steam-builds-without-the-steam-client) ·
[Managed code](#patching-managed-code) · [Startup](#startup-and-the-loading-screen) ·
[Textures and memory](#textures-and-memory) · [Performance](#performance) ·
[Firmware issues](#firmware-issues-seen-with-unity-4) · [Donor ports](#donor-ports) ·
[Testing and debugging](#testing-and-debugging) · [Logs to ask for](#logs-to-ask-for) · [Reference](#reference)

## How Unity 4 differs from Unity 5+

- The player draws with **desktop OpenGL** (GL 2.x, ARB programs and GLSL). There is no GLES renderer to switch
  to, so the GL calls go through [gl4es](../graphics.md#gl4es) (a GL-to-GLES translation library).
- Data files are **serialized file version 9** with **no type trees** in release builds: the player reads every
  object with its own built-in layouts. UnityPy reads them, but a 90-line reader is enough
  ([`unity4file.py`](../../unity/converters/unity4/unity4file.py)) and uses far less memory (see
  [Textures and memory](#textures-and-memory)).
- Layout: `<Game>_Data/mainData` + `levelN` (Unity 5+: `globalgamemanagers` + `levelN`). `mainData` holds the
  first scene: `levelN` is scene N+1 in BuildSettings.
- Compiled shaders keep their ShaderLab text in the asset and are parsed at load; the GL variant is
  `SubProgram "opengl"`.

## Is the game a candidate?

| Check | How |
|--|--|
| Version | header of `mainData` (bytes 20 to 40), e.g. `4.7.1f1` |
| Mono, not IL2CPP | `Managed/*.dll`, no `GameAssembly` |
| A Linux x86_64 build | `<Game>.x86_64` + `<Game>_Data/Mono/x86_64/`. If there is none, see [Unity 4 donor ports](unity-4-donor.md) |
| OpenGL shader programs | shaders with Direct3D programs but no `opengl` program. Unity 4 Windows builds normally carry `opengl` next to `d3d9`/`d3d11`; Ascendant's Windows build had one exception, Unity's DX11-only `NoiseAndGrainDX11`, which is harmless |
| Native plugins | `Plugins/`: each needs a Linux x86_64 equivalent or a stub (see [Steam builds](#steam-builds-without-the-steam-client)) |
| Steam | DRM wrapper (SteamStub `.bind` section), ownership calls (`BIsSubscribed`, tickets), saves through Steam Remote Storage |

- **Check each store's build.** Ittle Dew on GOG is 4.7.1f1 x86_64; on Steam it is 4.3.4f1, 32-bit x86 only.
  Thomas Was Alone's Windows build (4.5.1) is newer than its 32-bit Linux build (4.3.1). GOG installers can hold
  two builds (Ascendant: `Ascendant.x86` and `Ascendant_64.x86_64`); take the data folder that has an x86_64
  player.
- A Windows exe's bitness does not matter (the exe is discarded), so a 32-bit Windows build needs no box32.
- Supporting one store's build is fine. Say so in the README and `port.json`, and have setup detect the other
  builds by their files (Steam: `libsteam_api.so`, `steam_api*.dll`, Steamworks plugins; Windows: the `.exe`) and
  stop with a clear `PATCH_FAIL_MSG`, since users do not read READMEs.
- Prefer the DRM-free store's build when there is a choice.

## The runtime: no Westonpack

Unity 4 ports run without Westonpack. The player needs an X11 display, GLX and the GL calls, and all three come
from inside the game process. How the X11 stand-in and the GLX host work, their traps and measurements against
Westonpack: [graphics](../graphics.md#without-westonpack-glx-on-the-firmwares-sdl2).

| Piece | What it does |
|--|--|
| box64 | runs the x86_64 player and Mono ([box64](../box64.md)) |
| `libX11.so.6` stand-in ([xstub](../../shim/xstub/)) | answers the player's Xlib calls locally, keyboard from evdev |
| `libglxsdl.so` ([xstub](../../shim/xstub/)) | GLX for the pass-through gl4es build, on the firmware's SDL2: one fullscreen window, one GLES 2 context |
| gl4es | desktop GL on GLES 2, built from source with the ports' fixes (below) |
| `libGLU.so.1`, `libXcursor.so.1` | tiny stubs: the player only imports `gluErrorString` and 4 Xcursor functions |

- Launcher: `LD_LIBRARY_PATH=$GAMEDIR/libs.aarch64 LD_PRELOAD=<gl4es>:libglxsdl.so` around the box64 line; no
  runtime mount, `"runtime": []` in `port.json`.
- box64 needs a small patch so that missing helper libraries of a wrapped library (libxcb, libXfixes,
  libXrender) are not errors: [`box64/`](../../box64/).
- Ittle Dew's player needed 72 X11 functions.
- **ArkOS-family SDL2 builds** (dArkOS on the RG353V) replace box64's crash signal handlers, and Mono then died at
  its first JIT write: the GLX host saves and restores them
  ([box64](../box64.md#signals-and-native-libraries-in-the-box64-process)).
- Build anything with C++ in a Debian bullseye arm64 chroot with glibc and libstdc++ linked dynamically
  ([building native code](../building-native-code.md)). PortMaster does not accept statically linked core
  libraries.

### The Westonpack route (earlier ports)

Usagi and the first Ittle Dew releases ran in Westonpack's `crusty_glx_gl4es` mode. The player links libGL,
libGLU, libX11, libXcursor (4.5 also libXext and libXrandr) directly, and Westonpack's `lib_aarch64` has native
versions of all of them. Lessons from that route that still apply:

- Westonpack 0.2.7.1's gl4es (v1.1.7) prints a debug line on every `glReadPixels` / `glCopyTexSubImage2D` (no
  variable turns it off) and has the ARB program id bug below. To use another gl4es in that mode, preload it with
  `WRAPPED_PRELOAD_MALI`: putting it in a library folder is not enough, because Westonpack's own gl4es path comes
  first in `LD_LIBRARY_PATH`.
- With `CRUSTY_SHOW_CURSOR=1` crusty draws a pointer for mouse-driven menus; `CRUSTY_BLOCK_INPUT=1` is the one
  crusty variable a Unity 4 port needs. `CRUSTY_FPS=1` prints the frame rate into the game's stdout.
- Details of Westonpack and crusty: [graphics](../graphics.md#westonpack-and-crusty).

## gl4es and Unity 4

What gl4es costs, its Mali profile and its bugs are on [graphics](../graphics.md#gl4es). The Unity 4 specific part:

- The ports ship gl4es built from upstream with a patch ([`ship-fixes.patch`](../../devtools/gl4es-test/patches/))
  and the [gl4es test bed](../../devtools/gl4es-test/) that found most of the fixes. The ones every Unity 4 game
  hits:
  - **Crash at scene changes**: `getUniqueProgramID()` recycled freed ARB program ids, and gl4es's program cache
    then returned a program built for deleted ones (SIGSEGV in `GoUniformfv`). Any game with Unity 4 "opengl"
    shaders uses ARB programs. Ittle Dew crashed near the end of its intro.
  - **Point lights draw nothing**: gl4es read `GL_UNSIGNED_SHORT` pixels as bytes, and Unity 4 uploads its
    point-light attenuation texture as 1024x1 `GL_ALPHA16`. Teslagrad's menu highlight and street lamps are point
    lights. Still on gl4es master as of Jul 2026.
  - **Main FBO rejected by Mali**: `createMainFBO` used separate stencil and depth renderbuffers; Mali needs one
    packed `DEPTH24_STENCIL8`. Needed for the letterbox (see [Screen shapes](#screen-shapes-169-on-every-screen)).
  - ARB parser heap overflows that Teslagrad's shaders triggered (abort in `glProgramStringARB`), more than 24
    fragment locals (Unity's shadow collector has 27).
- **Unity 4 shaders in your own project (a donor): write Cg, not `GLSLPROGRAM`.** Raw GLSL ran on Mesa and
  through gl4es on the PC, but on the RG353V every texture not uploaded in the current frame sampled opaque black.
  Cg (compiled to ARB programs like every real game) fixed it.
- The player imports the GL 1.x core (`glTexImage2D`, draws, clears) directly and asks `glXGetProcAddress` only
  for extension functions (FBOs, compressed textures), so a hook in the GLX layer cannot catch core calls. Use an
  x86 preload for those ([fpslog](../../devtools/profiling/fpslog/)).
- Library defaults: check gl4es's `src/gl/init.c` before passing variables. `LIBGL_ES=2`, `LIBGL_GL=21` and
  `LIBGL_SILENTSTUB=1` are already the defaults. `LIBGL_AVOID16BITS` defaults to 1 on every GPU except PowerVR,
  so `LIBGL_AVOID16BITS=0` does change something (16-bit textures, see below).
- Variables that broke Unity 4: `LIBGL_SHRINK` modes 1, 2, 5 and 7 render the world black (render targets);
  mode 10 works but saves only ~40 MB. `LIBGL_BATCH=1` kills the game.
- **Set `LIBGL_NOBGRA=1`**: it keeps Unity's BGRA render and grab textures RGBA, so gl4es copies them on the GPU
  instead of through the CPU (why, and the measurements per device: [graphics](../graphics.md#what-it-costs)).
  The Unity 4 ports set it on every device.

## Player fixes

Three byte patches every Unity 4.5 to 4.7 x86_64 player needs. They can be found by pattern, so one script
covers every game: the patterns sat in every player checked (a 4.5.0 player version-patched for 4.5.3 data,
Ittle Dew's 4.7.1, Teslagrad's 4.7.2). Apply them in setup on the user's copy, SHA-1 checked, idempotent
(`printf '\xNN' | dd conv=notrunc` needs no Python). The byte patterns are in [Reference](#reference).

| Fix | Why |
|--|--|
| **VRAM floor** | Without a memory-info GL extension (gl4es offers none) the player assumes 64 MB ("VRAM: 64 MB" in the log), shrinks big textures ("Texture2D 4096x4096 won't fit, reducing size") and the reduced DXT textures render as horizontal stripes. Raising the floor to 128 MB fixed it |
| **Joystick reader off** | the player opens `/dev/input/js%d` itself, so with gptokeyb2 every press arrives twice (game binding + keyboard). Rename the string so the open fails |
| **Resolution list** | see [Screen shapes](#screen-shapes-169-on-every-screen) |

- Without the joystick fix, the alternative is to neutralise the game's own pad bindings in the data: every
  "joystick button N" in the InputManager rebound to a joystick that never exists
  ([`patch_input_axes.py`](../../unity/converters/unity4/patch_input_axes.py)), or a game's own input library
  bindings set to `"None"` (Usagi's cInput `Joystick1ButtonN`).
- **Resolution dialog**: Unity 4 players skip the GTK resolution dialog when `Plugins/x86_64/ScreenSelector.so`
  is missing (they log an error and continue). Delete it in setup. The dialog crashed on some X servers and is
  useless on a handheld.

## Screen shapes: 16:9 on every screen

- Unity 4 has no aspect handling. Perspective cameras keep the vertical view, so a 1:1 screen loses ~22% of each
  room per side (Teslagrad); 2D games designed for 16:9 crop their menus on 4:3 and 1:1 screens.
- Unity 4 **stretches** its picture when its fullscreen resolution differs from the desktop.
- The route that works for any game without touching its data: tell the game its desktop is the 16:9 fit of the
  screen (xstub's `XSTUB_SCREEN=WxH`, computed by the launcher and passed to setup) and let glxsdl present
  gl4es's main FBO centred. The mechanism: [graphics](../graphics.md#closed-games-a-169-letterbox).
- Report the 16:9 size as the desktop. Keeping a fullscreen window smaller than the reported desktop made Unity
  re-request fullscreen every ~2.5 s (choppy loading, dips).
- **Unity 4's built-in resolution list**: the player has no XRandR; `Screen.resolutions` comes from a table of 49
  sizes (640x480 to 2880x1800) filtered to the desktop size. Below 640x480 (every small 16:9 fit, and 480x320)
  the list is empty, and games that index it throw: Teslagrad's pause menu had no buttons
  (`IndexOutOfRangeException` in its GUI setup). Fix: when the 16:9 size is not in the list, write it over the
  first entry (offsets in [Reference](#reference)). Do it even when a smaller listed size fits: on a 1440x1080
  screen the area 1440x810 is not listed while 1280x720 is, and a game that matches its saved size against the
  list switches to the listed size, which Unity then stretches.
- Result on the RG Cube XX (720x720): Teslagrad at 720x405, 50-60 fps where full 720x720 ran 20-30.
- With the letterbox the game always renders into the main FBO on non-16:9 screens, so window-surface copies (see
  [Performance](#performance)) do not happen there at all.

### Games that letterbox themselves

Some games set camera rects for their own letterbox (Ittle Dew: a 1280x720 render target, camera rect
`y = n/2, h = 1-n` with `n = 1 - aspect/(16/9)`). Read the game's letterbox code before assuming a fix is
aspect-specific.

- **Bars keep old frames on the device.** Nothing clears outside a camera's rect; on the PC the bars stay black,
  on the handheld the swap chain returns stale buffers. Fix: one extra camera with clear flags 2 (solid black),
  culling mask 0, full rect, and the **lowest depth in the game** (e.g. -100). Ittle Dew's first try used depth 1
  and wiped the shop, which draws with its own low-depth camera. Prove it on the PC with a red clear colour.
- Put that camera on a GameObject that survives every scene (`DontDestroyOnLoad`, in the first scene, i.e.
  `mainData`).
- Menus cut off on 1:1 or 4:3: widening the orthographic size exposed cutscene staging outside the frame.
  Writing letterbox rects (`y`, `height`) into the full-rect cameras of the loading, intro, title and outro scenes,
  per screen shape from a `case` table in setup, plus the clear camera, held. Confirmed on the RG Cube XX
  (Knulli, 720x720). Camera field offsets are in [Reference](#reference).
- On the device, check the real picture with `cat /dev/fb0` where the firmware exposes the scanout (Knulli does),
  not with a PC run whose framebuffer is black anyway.

## Input

- gptokeyb2 maps the pad to keyboard and mouse; turn the player's own joystick reading off (Player fixes above).
- gptokeyb2's kill name is the game's `comm`, not box64's: box64 renames the process to the x86 binary's name
  (truncated to 15 characters, e.g. `IttleDew.x86_64`).
- **Mouse-only PC menus** (games that came from mobile): do not trust input code seen in decompiled scripts.
  Usagi's PC build had stripped its controller UI (empty `if` bodies, coroutines never started, an emptied
  `SetPointerPos`). Test with keys on the real PC game first.
  - Practical fix: right stick = `mouse_movement_*`, a free button (R3) = `mouse_left`. A visible pointer has to
    come from the display layer.
  - **`mouse_scale` is a speed multiplier**: in gptokeyb2 it is an alias of `deadzone_scale`, and the stick
    vector is multiplied by it, so higher is faster (default 512). Values used in the ports: 2048 on 640x480,
    1024 at 1080p. Tune it on the device.
  - Patch the few mouse-only gameplay moments (click or hover exits) to also accept keys (see
    [Patching managed code](#patching-managed-code)).

## Steam builds without the Steam client

- A Steam build often quits at once: `SteamAPI.RestartAppIfNecessary` returns true without the client. Put the
  game's app ID in **`steam_appid.txt`** next to the binary (written by setup). Steam's own library then returns
  false and `SteamAPI_Init` fails cleanly; the game's Steam code and libraries stay unmodified. Explain this in the
  README. PortMaster does not accept Steam emulators or DRM bypasses, so check for a DRM wrapper and ownership
  calls first.
- A Steamworks wrapper whose native library is missing can quit too. List its P/Invokes with
  [dnfile](https://github.com/malwarefrank/dnfile) (`ImplMap`: module + import name) and build an x86_64 `.so`
  exporting every name as `long f(void){return 0;}`: Init = false, RestartAppIfNecessary = false, the game goes
  on ([`steamstub`](../../unity/patching/steamstub/)). This does not fake ownership or emulate Steam:
  every call fails, as it would with no client running. Some games catch the `DllNotFoundException` themselves
  (Ascendant).
- **Check where saves go.** Some PC builds save only through Steam:
  - Teslagrad saved only through `SteamRemoteStorage`, and its achievement call threw before a pickup was saved.
    A patch of the game assembly points the save methods at the game's own pre-cloud local file layout and makes
    achievements no-ops.
  - Ittle Dew's Steam build has both backends and picks Steam only because one Steam-only method calls
    `Platform.OverrideSteam(true)`. One IL byte (`ldc.i4.1` -> `ldc.i4.0`) before that call makes it save to files
    like the GOG build. To find such a switch, list the callers of the backend selector in both stores'
    `Assembly-CSharp.dll` (MemberRef token in `call`/`callvirt`) and diff them. Prefer the DRM-free path over
    stubbing Steam.

## Patching managed code

Patches go into setup and run on the user's copy. A shipped patch should contain only the port's own bytes.

- **Lock to the exact DLL** (SHA-1) and refuse anything else.
- **In-place IL byte patch** (no Python or dnlib on the device): find dead code in the target method (platform
  branches such as `Platform.IsOuya`, constant comparisons) and rewrite those bytes using member tokens the
  assembly already has; pad with `nop` and stay under the header's max stack (opcodes in [Reference](#reference)).
  Ittle Dew: 29 bytes turn `height = 720` into `Mathf.Min(Screen.height, Screen.width*9/16)`. A dnlib rewrite of
  the same change made a 263 KB xdelta (whole-file rewrite), so prefer the in-place patch when it fits.
- **New method bodies in pure Python** (Usagi): append a PE section, write a new **fat method body** = the port's
  own IL prefix (e.g. a `GetKeyDown` check that jumps to the exit code) + **the original IL copied from the user's
  file**, and repoint the MethodDef RVA. Branches in the original IL are relative, so they stay valid after the
  prefix; exception clauses would need their offsets shifted (the patcher asserts there are none). Max stack =
  max(own, original); keep the original locals signature. String changes: swap `ldstr` tokens to an existing
  string in `#US`. Example: [`scriptpatch_usagi.py`](../../unity/patching/scriptpatch_usagi.py).
- On the PC: [dnlib](https://github.com/0xd4d/dnlib) to prototype and to dump tokens, RVAs and file offsets
  (`Metadata.PEImage.ToFileOffset(method.RVA)` + `Body.HeaderSize`; [example](../../unity/patching/dnlib-example/)),
  `ModuleWriterOptions` with `KeepOldMaxStack` (some UnityScript methods fail max-stack recalculation), and
  `ilspycmd` (.NET 8 with `DOTNET_ROLL_FORWARD=Major`) to check every result by decompiling it. Mono 2.6 in Unity 4
  loads all of these.
- A small xdelta of a one-byte change (87 bytes for Ittle Dew's Steam save switch) also works; xdelta3 checks the
  source, so a different game version fails setup cleanly.

## Startup and the loading screen

- **Seed `$HOME/.config/unity3d/global.prefs`** with `<pref name="StandaloneStatsDone" type="string">eWVz</pref>`
  before the first launch. Without it the player forks a copy of itself to collect hardware statistics
  (`/usr/bin/lsb_release`, which Knulli does not have). Under box64 that child's teardown cost 3-5 s on the first
  launch (about 3 s of it only when a frame had been drawn before the fork; 23 s with a cold box64 cache). With
  the file no launch forks, and Unity never tries to collect or send statistics from the handheld.
- glxsdl draws "Loading..." with scissored `glClear` calls only (a built-in 5x8 pixel font: no shader, texture or
  font file) at window creation, and puts it back after the game's black loading frames: while it is active, it
  samples the back buffer after each swap and redraws on a black frame; the first frame with content ends it.
  Splash logos fooled a four-pixel check (Teslagrad), so it samples five full-width rows. `GLXSDL_LOADING=0` turns
  it off.
- Timeline on the RG Cube XX (Knulli), Oct 2026, from picking the game in the menu: 2.9 s of EmulationStation and
  the firmware's launcher, the box64 process at 3.4 s, "Loading..." at 4.2 s, the first scene at 10.4 s.
- Loading time (box64 + the Mono JIT) cannot be fixed with Unity's knobs: `backgroundLoadingPriority` Normal or
  Low made Teslagrad's menu appear 11-14 s later; skipping `Shader.WarmupAllShaders` saved ~1 s in a single run
  (within noise, and it moves the cost to first-use hitches). Measure variants before changing game code.

## Textures and memory

How to measure memory, where Ittle Dew's memory went and the order of texture savings are on
[performance and memory](../performance-and-memory.md#measure-where-the-memory-goes); what gl4es's DXT decode
costs is on [graphics](../graphics.md#what-it-costs). The Unity 4 texture details:

- Unity 4.7's desktop player uploads only ARGB32 / RGBA32 / BGRA32 / RGB24 / Alpha8 / DXT ("Unsupported texture
  format" otherwise); a texture stored as real ASTC is decoded on the CPU to RGBA8. gl4es advertises only S3TC and
  **decodes DXT on the CPU** at upload into 16-bit or 32-bit pixels in RAM, which Mali shares: ~270 MB for Ittle
  Dew.
- **16-bit textures** (`LIBGL_AVOID16BITS=0`, DXT5 -> RGBA4444) saved 222 MB of peak RSS for Ittle Dew on the PC
  and were indistinguishable at 8x zoom on its portraits. Compare A/B screenshots before paying for 32-bit.
- **Uncompressed textures -> DXT** (Usagi, 4.5): 1023 -> 254 MB, median 41.5 dB, lettering pages kept
  uncompressed ([`textures_dxt.py`](../../unity/converters/unity4/textures_dxt.py), etcpak `compress_bc1/bc3`
  with RGBA input; decode with texture2ddecoder, since `etcpak.decompress_*` segfaults).
- **ASTC under the DXT5 label**: DXT5 and ASTC 4x4 both take 16 bytes per 4x4 block. Setup re-encodes the textures
  as ASTC 4x4 and keeps the DXT5 label; glxsdl relabels those compressed uploads as ASTC, which gl4es passes
  straight to a GPU that supports it. Where the GPU has no ASTC, glxsdl decodes on the CPU into RGBA4444 (at
  half size on low-memory installs). DXT1 and uncompressed textures are decoded, encoded as ASTC 4x4 and
  relabelled DXT5 too; readable textures (the game may read their pixels) and sizes that are not whole 4x4 blocks
  are skipped. Never run the re-encode twice on a file: ASTC blocks decoded as DXT5 are garbage. Tool:
  [unity4shrink](../../unity/converters/unity4/unity4shrink/) `-a` (with `-M`: full mip chains, so gl4es does not
  generate them at load).
- Ittle Dew on the RG353V (dArkOS), full-size textures, Oct 2026:

  | Textures | Launch to world | World load | Game RSS | Available |
  |--|--|--|--|--|
  | DXT + gl4es-generated mipmaps (earlier release) | 45.3 s | 35.0-35.5 s | ~1000 MB | ~725 MB |
  | DXT, no mipmaps | 38.1 s | 28.6-28.9 s | ~906 MB | ~815 MB |
  | ASTC 4x4, no mipmaps | 35.3 s | 26.5-26.8 s | ~764 MB | ~958 MB |
  | ASTC 4x4 with mip chains (shipped) | 36.0-36.6 s | 26.6-27.4 s | ~850 MB | |

  On the RG Cube XX (1 GB) ASTC at half size used the same memory as the earlier quarter-size DXT, at the same
  frame rate, and looked visibly sharper.
- **1 GB devices**: [unity4shrink](../../unity/converters/unity4/unity4shrink/) `-l 1` / `-l 2` halves or quarters
  large DXT textures in place (alpha-weighted averaging, written to `<file>.tmp` and renamed, output identical on
  x86_64 and aarch64). Ittle Dew's texture data went 320 -> 80 MB at quarter size. Before shrinking, check the
  game: Sprite objects (4.3+ sprites store pixel rects), scripts reading `texture.width`, NGUI or 2D Toolkit pixel
  atlases, SpriteManager 2 `pixelPerfect` (a halved texture halves the sprite). Ittle Dew's SpriteManager 2 sprites
  kept their layout, slightly softer.
- Decide what to shrink by residency, not by size ([method](../performance-and-memory.md#reducing-it));
  [`tex_inventory.py`](../../unity/converters/unity4/tex_inventory.py) lists a Unity 4 game's large textures by
  name and file.
- Offer the choice in setup: PortMaster's patcher can ask a question (`PATCHER_QUESTIONS`), e.g. automatic (half
  size under 1.5 GB of RAM, full above), full, half, quarter, unchanged. `MemTotal` of every "1 GB" device is a bit
  under 1 GiB, so test against 1.5 GiB.
- **No UnityPy on the device**: parse the files directly ([`unity4file.py`](../../unity/converters/unity4/unity4file.py);
  file and Texture2D layouts in [Reference](#reference)). Rewriting = streaming objects from an mmap into `.tmp`,
  fsync, replace. Usagi's texture pass peaked at 183 MB of private memory against ~1.3 GB with UnityPy, and the
  output objects were byte-identical.
- Teslagrad on the RG Cube XX peaked at ~500 MB with at least 330 MB free and no swap.

## Performance

The general method (CPU per frame, thread samples, governors) is on
[performance and memory](../performance-and-memory.md); box64 settings and Mono GC settings for a JIT process are
on [box64](../box64.md#what-was-tried-with-mono-a-jit-inside-the-emulated-process). Unity 4 specifics:

- **30 fps on these devices is often a vsync step**, not the game's speed: any frame over 16.7 ms lands on 30.
  Compare variants by CPU time per frame and the busiest thread's share.
- **Two kinds of per-frame copy**, both CPU readbacks through gl4es unless `LIBGL_NOBGRA=1`:
  1. `GrabPass` shaders (`grep -a -c GrabPass *.assets` finds them).
  2. **Unity's own window grab for an image effect** (`OnRenderImage`) on a camera that draws to the screen: Unity
     4 copies the camera's viewport into the effect's source texture every frame.
- Count them with the [fpslog](../../devtools/profiling/fpslog/) preload (fps, longest frame, grabs and readbacks
  per second in `unity.log`).
- **Neutralise a GrabPass without code**: the ShaderLab text is parsed at load, so a same-length edit works:
  `GrabPass {\n }` (13 bytes) -> `ColorMask 0  `. Ittle Dew lost a heat shimmer and one of two grabs per frame;
  ~1 ms per frame once copies were on the GPU.
- **Fixed-size render targets**: 2D games often render into a target of the design resolution (Ittle Dew:
  1280x720 + a lighting target) and scale it: 4-5x the pixels on 640x480 or 480x320. Capping it to the shown area
  (`Mathf.Min(Screen.width * 9 / 16, 540)` fits the same 29-byte IL slot as above) cuts fill rate, copies and
  memory, but without mipmaps the world aliases at 2:1 minification, so do it where textures are already reduced.
- **Moving an image effect off the screen camera** (scene edit without an editor): add a new camera that copies
  the screen camera, renders into a new RenderTexture at a depth just below it, and takes over the effect
  components (move effects that look up sibling components together). The old camera keeps its scripts and only
  changes its culling mask to an unused layer holding one plane that shows the texture. The effect then runs
  texture to texture, with no window copy.
  - Unity 4 scene files have no spare room in the object table: rebuild the file from its pieces and fix the
    header (steps in [Reference](#reference)). Shrinking an object in place is fine.
  - Layouts for classes without type trees: UnityPy's bundled type tree package
    (`UnityPy.helpers.Tpk.get_typetree_node(class_id, (4, 7, 1, 0))`); `obj.read_typetree()` on the rebuilt file
    is a quick parse check.
- **AOT for Unity 4's Mono (2.6.5)**: its `libmono.so` has the AOT compiler built in. A 3-line host
  (`main -> mono_main`) linked against it, then
  `--aot=asmonly,bind-to-runtime-version,outfile=x.s Assembly.dll`, `as`, `gcc -shared -o Assembly.dll.so`
  (`--aot=outfile=x.so` crashes in Mono's own link step). The player loads `<dll>.so` from beside every assembly
  (check with `MONO_LOG_LEVEL=info MONO_LOG_MASK=aot`). Compile from the DLL as shipped after any byte patch (the
  GUID is checked). RG Cube XX, Sep 2026: -4% CPU per frame alone, -8% stacked with other changes. Later launches
  also get faster through box64's [DynaCache](../box64.md#dynacache).
- Performance governor on the RG Cube XX (Sep 2026): -19% CPU per frame at 30 fps, little at 60.
- Teslagrad on the RG351P (RK3326, ROCKNIX, Panfrost), Oct 2026: title 17 fps, late scenes 12-26 fps, main thread
  at ~90% of one A35 core, GPU fragment engine 39% busy: CPU-bound. Ittle Dew does 31 fps in its world there.

## Firmware issues seen with Unity 4

- **No System V IPC on the H700 kernels of Knulli and muOS**: the player spins at 100% on one thread right after
  the `Mono path` lines, before any window. Set **`MONO_DISABLE_SHM=1`** (check that the libmono honours it:
  `grep -a MONO_DISABLE_SHM libmono.so`; otherwise the [sysvsem](../../shim/sysvsem/) preload). Kernel details
  and dates: [devices and firmwares](../devices-and-firmwares.md#knulli).
- **muOS + PipeWire** (TrimUI Brick, Sep 2026, Westonpack): the game hung 2 of 3 times entering the world, inside
  FMOD's output through PipeWire's ALSA plugin. `PIPEWIRE_LATENCY=1024/48000` fixed it.
- **Audio needs `XDG_RUNTIME_DIR`** where PipeWire is used: with it missing, Unity 4's FMOD logs
  `Error initializing output device`, and every later sound call logs `An invalid object handle was used`
  (thousands of lines, from FMOD, not Unity).
- **Config and saves**: Unity 4 uses `$HOME/.config/unity3d/<company>/<product>` and ignores `XDG_CONFIG_HOME`.
  Pass `HOME=$CONFDIR`; saves (`Application.persistentDataPath`) then land in `conf/.config/unity3d/...`. Check the
  README's save path against the log line `Creating user config folder`.
- A player that hangs before its first display connection is a firmware or kernel question, not a graphics one.

## Donor ports

A Unity 4 game with only a Windows, Mac or 32-bit Linux build can run on a Linux x86_64 player you build yourself
with the Unity 4.7.2 editor (the donor): setup converts the game's data to the donor's version on the device.
Tested from 4.0 to 4.7 data on the PC, and on the RG353V and RG Cube XX (Oct 2026). The technique, the version
conversion rules and the results: [Unity 4 donor ports](unity-4-donor.md).

## Testing and debugging

General methods: [testing and debugging](../testing-and-debugging.md). Unity 4 specifics:

- WSLg breaks Unity 4 players (resolution dialog crash, then "Display is invalid"). Use Xvfb **plus a window
  manager** (Openbox; without one, keyboard and mouse do not reach the game), sized to the device resolution
  (the game forces fullscreen).
- gl4es on Mesa inside a Unity 4 process needs `LIBGL_NOTEST=1`, which leaves gl4es with almost no capabilities
  (black world, HUD fine). Test with the Mali profile before chasing a gl4es rendering bug on the PC
  ([graphics](../graphics.md#testing-gl4es-on-the-pc)).
- When a picture is wrong, trace one frame end to end: GL calls the game gets through `glXGetProcAddress` from a
  debug glxsdl, directly imported calls from the x86 preload, both into one file while a trigger file exists.
  Teslagrad's empty pause menu drew 2 quads instead of 12: a CPU-side problem.
- A test-only build that loads a save from the first scene (replace the startup method with the menu's own "load
  slot 1" path) got Ittle Dew to gameplay in ~55 s on the RG351P. Never ship it.
- `-logFile` can be a FIFO for a live log filter, but the player reopens the path later; where the kernel refuses
  that reopen (`fs.protected_fifos`), Unity falls back to `$HOME/.config/unity3d/Player.log`
  ([testing and debugging](../testing-and-debugging.md#filtering-a-noisy-log-through-a-fifo)). A plain
  `unity.log` is simpler.

## Logs to ask for

Ask for `log.txt` (the launcher, gptokeyb2 and box64) and `unity.log` (the player's `-logFile`; if it is missing,
`Player.log` under `conf/.config/unity3d/`). What to look for:

| Line | Meaning |
|--|--|
| firmware, kernel and glibc lines of the launcher's environment dump (`log.txt`) | read them before touching the port; an old firmware release or kernel explains many hangs |
| `Mono path` lines, then nothing (100% CPU on one thread) | no System V IPC: [firmware issues](#firmware-issues-seen-with-unity-4) |
| `VRAM: 64 MB`, `Texture2D 4096x4096 won't fit, reducing size` | the VRAM floor fix is missing: [player fixes](#player-fixes) |
| `/dev/input/js0` opened by the player | the joystick reader is on: presses arrive twice |
| `Error loading ... ScreenSelector.so` | expected: the resolution dialog is skipped |
| `Creating user config folder: <path>` | where config and saves go |
| `Loaded Objects now: N` | a scene finished loading (Ittle Dew's world: ~7050); gives the session shape |
| `Error initializing output device`, then thousands of `An invalid object handle was used` | FMOD has no audio output (`XDG_RUNTIME_DIR`, PipeWire): [firmware issues](#firmware-issues-seen-with-unity-4) |
| `Unsupported texture format` | a texture in a format the 4.7 desktop player does not upload |
| `IndexOutOfRangeException` in GUI setup | an empty resolution list: [screen shapes](#screen-shapes-169-on-every-screen) |
| `Invalid serialized file version`, `Mismatched serialization`, `Failed to read file`, `MissingMethodException` | donor and version problems: [donor ports](unity-4-donor.md#versions) |
| fpslog lines (fps, longest frame, grabs, readbacks) | per-frame copies: [performance](#performance) |
| box64 crash report (`BOX64_LOG=1 BOX64_SHOWSEGV=1 BOX64_SHOWBT=1`) | read `si_code` first ([box64](../box64.md#debugging-crashes)) |

- An exit code 137 right after `back` / `start` presses in gptokeyb2's lines is a Select+Start quit, not a crash.

## Reference

Byte-level details behind the sections above (Unity 4.7 unless stated, release players without type trees).

**Player fix patterns** (x86_64 players 4.5 to 4.7):

| Fix | Pattern |
|--|--|
| VRAM floor | the clamp `48 81 3d <rel32> ff ff ff 03 77 <rel8> 48 c7 05 <rel32> 00 00 00 04`: raise both immediates (to 128 MB) |
| Joystick reader off | the string `/dev/input/js%d`: rename it |
| Resolution list | the built-in size table (an entry 640x480, then 10+ sizes sorted by height); first entry at file offset 0xf14420 in 4.7.1/4.7.2, 0x1080660 in the 4.5.0 player used for 4.5.3 data |

**Camera object** offsets: +8 `m_Enabled`, +12 clear flags, +16 colour, +32 rect (`x y w h`), +64 orthographic
size, +68 depth; culling mask after depth.

**IL opcodes** used for in-place patches: `ldarg.N` 02-05, `call` 28 + token, `ldc.i4.s` 1F, `ldc.i4` 20, `mul`
5A, `div` 5B, `stind.i4` 54, `nop` 00 as padding; `ldc.i4.1` 17, `ldc.i4.0` 16.

**PE section** for new method bodies: characteristics 0x60000020; fix SizeOfImage and the section count.

**Serialized file** (version 9): the header is big-endian (metadata size, file size, version 9, data offset,
endianness byte). The object table (`count x {pathID, byteStart, byteSize, typeID, short classID, short
destroyed}`) is found by scanning the metadata for a count whose entries tile the data section exactly (8-byte
aligned). Object starts are relative to the data offset.

**Rebuilding a scene file** with new objects: table, new 20-byte entries, externals, padding to 16 bytes, data,
new objects 8-byte aligned; then fix the big-endian header sizes, data offset and object count. Existing entries
do not change.

**Texture2D**: name (length + bytes, aligned to 4) | width, height, completeImageSize, format | mipMap,
isReadable, readAllowed, pad | imageCount, dimension | filter, aniso, mipBias, wrap | lightmapFormat, colorSpace |
image data length (at fields + 52) | data. Unity 4 keeps texture data inline and `m_MipMap` is a bool.
