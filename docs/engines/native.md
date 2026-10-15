# Native ports: source ports and reimplementations

Games with source code (or an open reimplementation of the engine) built as aarch64 binaries
against the firmware's SDL2: what to check before starting, SDL 1.2 and SDL3 code, D programs,
drawing with the SDL renderer or OpenGL 1.x, filling every screen, input, audio and first-launch
data. The generic build setup (old glibc, the bullseye chroot, clang cross builds) is in
[building native code](../building-native-code.md), the GL stack in [graphics](../graphics.md).

Evidence comes from the author's native ports: Hocus Pocus (OpenPocus fork), Torus Trooper and
Mu-cade (D), Cube (C++, SDL 1.2), OpenLoco (SDL3-only upstream), Stunt Playground (Ogre/Newton/CEGUI
rewrite), Bugdom 2, 3D Movie Maker, an overnight batch (Taisei, TBFTSS, The Legend of Edgar,
Domino-Chain, HyperRogue, Toppler, Simon Tatham's Puzzles, Planet Blupi, Open Surge), and an update
of an existing KeeperFX port. Most
were tested on the RG Cube XX (Allwinner H700, 4 x Cortex-A53 at 1.5 GHz, Mali-G31 blob, Knulli,
720x720) in Sep/Oct 2026; other firmwares are untested unless stated.

Contents: [Before starting](#before-starting), [Building](#building), [D programs](#d-programs),
[SDL 1.2 code on SDL2](#sdl-12-code-on-sdl2), [SDL3-only upstreams](#sdl3-only-upstreams),
[Drawing](#drawing), [Filling every screen shape](#filling-every-screen-shape), [Input](#input),
[Audio](#audio), [Replacing a whole middleware stack](#replacing-a-whole-middleware-stack),
[First-launch data](#first-launch-data), [Configuration and saves](#configuration-and-saves),
[Testing native ports](#testing-native-ports).

## Before starting

- **Search the catalogue by upstream name and description, not only the PortMaster title.**
  Toppler was already in PortMaster as "Nebulus" and was found only after it was built; titles
  with punctuation ("mr.boom") hide from a plain search. Check PortMaster's `ports.json` (title and
  description, punctuation stripped), the `ports/` tree of
  [PortMaster-New](https://github.com/PortsMaster/PortMaster-New) and its open PRs.
- **Look for earlier ports of the same author's games.** Five ABA Games titles were already in
  PortMaster, ported by Cebion from M-HT's D2/SDL2 forks (through gl4es), which settled the base
  for Torus Trooper and Mu-cade. A merged port of a similar game is the best template for the launcher.
- **Read the upstream README** for licence terms and contribution policy before spending time:
  Planet Blupi's says it does not accept AI-generated code.
- **Upstreams that moved to SDL3** often have a last SDL2 release: build that (Taisei 1.4.2,
  Bugdom 2 4.0.0). SDL3-only code needs a different route (see
  [SDL3-only upstreams](#sdl3-only-upstreams)).
- **Data rendered at build time** (POV-Ray scenes in Domino-Chain and Toppler): take the release
  tarball's or Debian's packaged data instead of rendering it.
- **Media licences**: if the code is free but the media are "individual copyrights, distribute the
  archive unmodified" (Cube), ship the original archive untouched and unpack it on the first launch
  (see [First-launch data](#first-launch-data)). Games whose data the user must own read it from
  the user's copy; never ship it.

## Building

The chroot, glibc targeting and the rule against static core libraries are on
[building native code](../building-native-code.md). What the native ports added:

- **Bundle nothing SDL.** SDL2, SDL2_image, SDL2_mixer and SDL2_ttf come from the firmware.
- **Link statically what no firmware has** instead of shipping a `.so`: libBulletML and GLU (Torus
  Trooper), ODE (Mu-cade), Lua, FriBidi and Boost (Domino-Chain), freetype/libpng/libwebp/zstd/opus
  (Taisei), image and audio codecs (Open Surge). libstdc++, libgcc and glibc stay dynamic.
- **Old C libraries** the game was written against are best built from the original tarball and
  linked statically. Mu-cade needs ODE 0.5 in single precision: newer ODE needs `dInitODE` and
  behaves differently.
- **Bundle a shared library only when it must be loaded at run time** or is large and common:
  OpenAL Soft (OpenLoco, with its ALSA and PulseAudio backends dlopen'd; KeeperFX), FluidSynth
  (3D Movie Maker), Ogre and Bullet (Stunt Playground, in `libs.aarch64/`).
- **The bullseye sysroot has older SDL2 headers** (SDL2 2.0.14, SDL2_ttf 2.0.15) than the firmwares
  (Knulli: SDL2 2.30, SDL2_ttf 2.24). Guard newer calls with `SDL_VERSION_ATLEAST`, or declare the
  few newer functions as weak symbols and call them only when present (Planet Blupi's SDL2_ttf 2.20
  script/direction calls).
- **Newer C++ than GCC 10** (OpenLoco's `using enum`): clang 18 + lld against the bullseye sysroot,
  as on [cross building with clang](../building-native-code.md#cross-building-with-clang); OpenLoco
  also needed `CMAKE_CXX_SCAN_FOR_MODULES=OFF`, relative library symlinks in the sysroot (absolute
  ones made lld pick `libdl.a`) and a libstdc++ 10 header overlay (`std::bit_cast`).
- Bugs the x86 build hides (uninitialised members, clang's `printf(std::string)` trap, unsigned
  `char`, a function named `link` clashing with POSIX `link()`) and build hygiene (delete the old
  binary first, rebuild from the README verbatim and compare byte for byte, as done for Torus
  Trooper) are on [building native code](../building-native-code.md#bugs-that-only-appear-on-arm).

## D programs

**Never use `std.math` or the `real` type in per-frame code of an aarch64 build.** On x86 `real` is
the FPU's 80-bit format, in hardware. On aarch64 it is IEEE quad precision (128 bit), computed in
software by libm's long double routines and libgcc's soft-float helpers. `std.math.sin(float)`
forwards to the `real` version, and `std.math.PI` is a `real`, so even `angle * 180 / PI` with a
float angle is quad arithmetic. The same applies to `long double` in C and C++ on aarch64.

Measured on the RG Cube XX (Torus Trooper, Sep 2026), the same 70 s scripted session:

| | `std.math` | C float math |
|--|--|--|
| fps in play | 6-17 | 56-61 |
| logic steps/s (62.5 = full speed) | 31-60 | 62.4-63.3 |
| building the frame | 38-49 ms | 6.6-9.6 ms |
| libm share of CPU | 78.5% | 5.4% |

- Fix: a module of the game's own that provides the names the code uses (`sin cos atan2 sqrt
  fabs isNaN`) as float and double functions from `core.stdc.math` (`sinf`, `cosf`, ...) and `PI`
  as `enum float`. Replace `import std.math;` with it, and the qualified `std.math.X` calls too.
  Provide both float and double overloads (float only rejects double arguments). With
  `-ffast-math`, test NaN by the bits (`x != x` may be optimised away).
- A PC cannot show this: x86 runs the same code in hardware. PC frame comparisons only confirm
  that the change of precision does not alter the game (at most 46 pixels per frame differed).
- Three renderer rewrites were made against a bottleneck guessed from PC tests and device FPS
  logs; none helped and one made it far worse. One profile on the device found the cause in a
  minute. Measure first: see [performance and memory](../performance-and-memory.md).
- D binaries still import seven `__*tf*` soft-quad helpers from the static Phobos (number
  formatting). Harmless: check per object file (`nm *.o`) that no game code uses them.
- GDC 10 from bullseye builds D2 code. `-static-libphobos` is fine; libstdc++ must stay dynamic,
  but gdc ignores `-static-libstdc++` and drops a bare `-lstdc++`: spell out
  `-Wl,--as-needed ... -Wl,-Bdynamic -lstdc++`. Result: GLIBC_2.17 and GLIBCXX_3.4.21 at most.

## SDL 1.2 code on SDL2

From Cube (2005, SDL 1.2 + OpenGL 1.x):

- Key numbers in text config files (`keymap.cfg`, every saved `config.cfg`) are SDL 1.2's.
  Translate SDL2 key codes back to the old numbers in one function (Cube's `sdl1key`) instead of
  changing the data, so existing configs keep working.
- SDL2 always repeats keys: drop events with `event.key.repeat` except while the player types.
  Text arrives as `SDL_TEXTINPUT`, the wheel is its own event (map it to the old buttons 4/5).
- `SDL_KillThread` does not exist: detach the threads instead.
- Create the window at the `SDL_GetDesktopDisplayMode` size when fullscreen. Then no window
  manager is needed (Xvfb without one ignores SDL's fullscreen) and devices behave the same.
- SDL_image can often go: `stb_image` loads PNG and JPEG with one dependency fewer.

## SDL3-only upstreams

OpenLoco has no SDL2 path, and some Sugar engine games (Spaghetti Celesti, Shotgun King; see
[macOS games through machismo](macos-machismo.md)) link SDL3. Firmwares ship SDL2, so these ports
bundle an SDL3 built on an SDL2 backend (an SDL3-on-SDL2 shim, as PortMaster's Arcanum CE port
does). Notes from OpenLoco:

- Build the SDL3 side with `SDL_GPU=OFF` when the game only uses `SDL_Renderer`: the GPU API pulls
  in SPIRV-Cross and a static libstdc++.
- Mouse motion: upstream guessed "relative" from `which != 0`; patched to always absolute.
- No cursor reaches the screen (see [Pointer and cursor](#pointer-and-cursor)).

## Drawing

### SDL renderer games

- **The firmware's SDL renderer on Mali is GLES.** Add `SDL_RenderSetLogicalSize` when the game
  assumes its window is its frame (TBFTSS forces at least 1280x720, Edgar 640x480).
- Then check every `SDL_RenderReadPixels`: it returns the *scaled* viewport, so a game-sized buffer
  overflows (Edgar's pause snapshot segfaulted).
- **Games that present only dirty rectangles** show partial frames, because the GLES back buffer
  is not kept between presents: copy the whole texture on each present (Domino-Chain).
- A 320x200 DOS game (Hocus Pocus): render to a target texture and present it in a centred 4:3
  box, with an environment switch for other shapes or fill. SDL's software renderer is enough.

### OpenGL 1.x games: gl4es or an own GLES 2 layer

gl4es translates desktop GL to GLES at run time (see [graphics](../graphics.md) for what it costs
and where ports get it). For a game with source, a GLES 2 layer compiled into the game is usually
far faster. [gles/](../../gles/) has two such layers to copy (a C one with lighting, two texture
units, fog and texgen from Bugdom 2; a CPU-batching C++ one from Cube).

| Game | Through gl4es | Own GLES 2 layer |
|--|--|--|
| Cube, RG Cube XX, Sep 2026 | 8-22 fps over five test maps | 60 fps (vsync) on four of them |
| Torus Trooper, RG Cube XX, Sep 2026 | 1,750-3,985 `glBegin` blocks per frame | 1 draw per frame; 56-61 fps once the D math was fixed |
| Bugdom 2, RG Cube XX, Oct 2026 | not tried | ~60 fps; matches the desktop GL build on all 10 levels (PC frame diffs) |

Why gl4es is slow here (draw counts, CPU split and frame times for Cube) is on
[graphics](../graphics.md#native-gles-2-when-you-have-the-source).

How the shipped layers work:

- Keep the game's GL 1.x calls; a header renames them (`gl1_*`). Load the ES 2.0 entry points with
  `SDL_GL_GetProcAddress`, link no GL library and bundle none, and ask SDL for an ES 2.0 context.
- Cube's and Torus Trooper's layers transform every vertex to clip space on the CPU and append it
  to one client-side array, so matrix changes never split a draw. Draw only when texture, blend,
  depth or similar state changes. Small game changes help: Cube's world strips sorted by texture,
  display lists dropped, GLU replaced by `glGenerateMipmap`.
- Torus Trooper's tricks: lines become quads one pixel wide along the minor axis (GL's own
  description of a wide line, so `glLineWidth(2)` works on every driver); colours premultiplied so
  one blend function (`GL_ONE, GL_ONE_MINUS_SRC_ALPHA`) covers blending off, alpha and additive;
  face culling by the sign of the clip-space determinant.
- `glTexImage2D` on ES 2 takes the format as the internal format.
- Make anything unsupported fail loudly (throw or log once), so a PC test shows it.

What the Mali blob taught these layers (draw-call cost, never streaming into one VBO with
`glBufferSubData`, no orphaning between two uploads of one draw) is on
[graphics](../graphics.md#mali-driver-lessons).

ES 2 limits that need workarounds:

- **No depth readback.** Cube aims at the depth under the crosshair with
  `glReadPixels(GL_DEPTH_COMPONENT)`. The layer keeps the frame's batches, draws the depth-writing
  ones again into a 1x1 RGBA8 FBO whose viewport puts the wanted pixel on it, with a shader that
  writes depth as 24 bits in r/g/b, and reads that pixel. Exact, since the same vertices rasterize
  the same.
- **Do not use `gl_FragCoord.z` for that.** GLSL ES 1.00 declares it mediump and drivers (Mesa too)
  keep it in fp16, which snapped depth to 1/2048 steps. Pass clip z and w in a highp varying and
  divide in the fragment shader.
- **No polygon offset for lines** (`GL_POLYGON_OFFSET_LINE`): wireframes z-fight unless the line
  vertices are pulled towards the viewer in clip space.

### Other renderers

- **Games with their own GLES path**: HyperRogue's web/Android shader path built with a new
  `HYPER_GLES` switch (GLES 3.0). Its desktop drawing defaults gave 14 fps on the RG Cube XX; the
  Android defaults (flat walls and monsters, no aura or particles) compiled in gave 27-28 fps
  (Oct 2026). Taisei's GLES 3.0 renderer needed its shaders translated to ESSL at build time;
  54-60 fps in stage 1 on default quality, menus ~30 fps (heavy background shader).
- **Ogre** (Stunt Playground): Debian's `libogre-1.12` GLES2 render system is desktop GL + GLX
  underneath, so Ogre 14 was built from source (GLES2 render system, RTSS for the fixed-function
  materials) with a headless EGL backend that renders into SDL's window and context (SDL swaps).
  Ogre loads plugins by their versioned names (`.so.14.3`). ~60 fps on the RG Cube XX.
- **Dear ImGui next to another renderer**: use the ES3 backend on an ES 3.0 context
  (`IMGUI_IMPL_OPENGL_ES3`). The ES2 backend rewrites the attribute pointers of the vertex array
  object the engine left bound (garbage geometry, black sky).
- **Allegro 5 on its SDL2 backend** (Open Surge): `al_acknowledge_resize` recreates every texture
  from RAM backups, and `al_flip_display` makes those backups only for bitmaps dirty at that flip,
  so bitmaps loaded between the last flip and the fullscreen resize come back blank (level tiles
  and backgrounds invisible, sprites fine). SDL on KMSDRM keeps GL textures over a resize: drop the
  recreate and the per-flip backups. That also cut RSS from ~525 MB to ~235-300 MB. The patch and a
  GTK-free native dialog stub are in [allegro/](../../allegro/). Such bugs were found by
  checksumming the texture (read-only lock) at checkpoints until the step that blanks it showed up.

## Filling every screen shape

Screens in use include 480x320, 640x480, 720x720, 854x480, 960x544, 1024x768, 1280x720 and
1440x1080; no PortMaster handheld is wider than 16:9 (as of Sep 2026). With the source, never
letterbox a 3D game: keep the original view inside and extend it. This section is the reference for
games with source; [graphics](../graphics.md#filling-every-screen-shape) covers closed games (a
16:9 letterbox) and links here.

- Wider than the original layout: widen the frustum sideways (Hor+). Narrower: make it taller
  (Vert+). Torus Trooper, Mu-cade, Cube and Bugdom 2 (1:1 Vert+, level titles were cut at the
  sides before) work this way.
- 2D overlay: make the ortho cover the extended layout (`-marginX .. W+marginX`) and move only
  what sits at an edge (score, frame corners, bottom icons) by the margin; centred things stay.
  Cube's HUD virtual screen is 1800 high and wider on wide screens, 2400 wide and taller on narrow
  ones.
- Anything that culls by the field of view (Cube's occlusion rays and `render_world`) must get the
  angle actually shown.
- A camera that follows the player with a clamp (Mu-cade) should clamp sooner by the extra view,
  so the view still reaches as far past the level's edge as on 4:3. On 16:9 Mu-cade then shows the
  whole field width.
- Watch for objects that stretch with the view: Torus Trooper's title torus stretched in all
  directions brought its near side so close that a straight edge showed at 20:9; stretching it
  sideways only, by (9 * extendX + 1) / 10, fixed that.
- 2D games drawn with the SDL renderer: `SDL_RenderSetLogicalSize` scales and centres the game's
  frame (see [SDL renderer games](#sdl-renderer-games)); a low-resolution DOS game can keep its 4:3
  box (Hocus Pocus).
- The GLES 2 tricks a 3D port may need along the way (depth readback without
  `GL_DEPTH_COMPONENT`, no `gl_FragCoord.z`, no line polygon offset) are under
  [ES 2 limits](#opengl-1x-games-gl4es-or-an-own-gles-2-layer) above.
- Check that 640x480 stays pixel-identical to the original (Torus Trooper: 0 differing pixels over
  5 frames), then dump frames at each size on Xvfb and look at them side by side (`aspect_test.sh`
  and contact sheets in [devtools/native-testing/](../../devtools/native-testing/)).

Engine-specific helpers: [LÖVE](love.md) (`fit.lua`), [Godot](godot.md) (stretch modes), Unity 4
([16:9 on every screen](unity-4.md#screen-shapes-169-on-every-screen)).

## Input

- **Read sticks through SDL's GameController API**, not raw joystick axis and button numbers,
  which differ per handheld. PortMaster exports `SDL_GAMECONTROLLERCONFIG`. Torus Trooper skips
  its raw joystick code under `version(PORTMASTER)`; Mu-cade reads both sticks through the
  GameController API (twin-stick, analog) and gets buttons as keys from gptokeyb2. Toppler's raw
  joystick code made every button, D-pad included, "fire".
- **gptokeyb2** turns the pad into keys or a mouse for games that read only those. Games with
  native controller support still use it for the quit hotkey only (Taisei, Bugdom 2, Open Surge).
  Plain gptokeyb (v1) without `-c` types its own default keys: Stunt Playground uses gptokeyb2 with
  an empty ini instead (the quit combination is built in).
- **Drop a game's bundled controller database** when the firmware's mapping is right: Taisei's
  `gamecontrollerdb` mapped the RG Cube XX's GUID as "ODROID Go 2".
- **The quit hotkey kills by name.** gptokeyb2 runs `pkill -9` on the binary name, and the kernel's
  process name (comm) is at most 15 characters. Keep binary names at 15 characters or less
  (`domino.aarch64`, `hrogue.aarch64`, `stuntpg.aarch64`) or pass a short prefix as gptokeyb2's
  first argument (`hocuspocus` for `hocuspocus.aarch64`). Launcher rules:
  [packaging](../packaging.md#shape); what busybox `pkill`/`pgrep` do on each firmware:
  [devices and firmwares](../devices-and-firmwares.md#firmwares).
- **The quit is SIGKILL**: games that save only on exit lose progress. Autosave after changes
  (the Tatham Puzzles front end saves 2 s after each move).
- **Prompts and dialogs**: keyboard-only games driven by gptokeyb2 need their prompts changed to
  pad names (START, SELECT, A/B, L1/R1) and yes/no boxes answered by buttons, or the pad cannot
  answer them (Hocus Pocus: fire = yes, jump = no).
- **Mouse look from a stick** (Cube): with gptokeyb2's `deadzone_scale = 8` a full stick moves
  8 px per 16 ms tick (500 px/s), about 45 degrees per second in Cube; 32 gives about 180 degrees
  per second. OpenLoco's map mouse felt slow at 8 and uses 10.
- **ImGui gamepad navigation** was awkward in Stunt Playground's menus; a stick mouse (left stick or
  D-pad pointer, A click, right stick scroll) worked better on the device.
- **Text entry**: OpenLoco preloads Cebion's OmniOSK on-screen keyboard (SDL3 build) on R3. Lessons:
  gptokeyb2 cannot tell a tap from a hold, so a Select+L2 combo sent Escape first and closed the
  text box; map the keyboard's keys onto what the buttons already send instead of a gptokeyb2 layer
  (OmniOSK closes itself, a layer would get out of step); `instant` mode types straight into the
  game's box (buffered mode kept its own line and wiped the game's). OpenLoco itself attached only
  the first character of a text event to the last key press, so text without key presses (OSK,
  IMEs) was lost: patched to queue every character.

### Pointer and cursor

KMSDRM and Mali fbdev have no hardware cursor, and the SDL3-on-SDL2 shim forwards none. Games that
use `SDL_CreateColorCursor` or the system cursor show no pointer (Planet Blupi, OpenLoco). Hide the
system cursor and draw the game's cursor sprite into the frame before present (logical position
from `SDL_GetMouseState`, the render scale and the viewport). Stunt Playground shows the cursor only
once a mouse moves.

## Audio

- SDL_mixer comes from the firmware. Music in MIDI needs a soundfont: `SDL_SOUNDFONTS` pointing at
  a bundled TimGM6mb.sf2 (Debian `timgm6mb-soundfont`, GPL-2, 5.7 MB) is the light option.
- **Old OGG files can play muffled.** Files encoded before libvorbis 1.0 (2001 and earlier) may use
  residue type 0. SDL_mixer 2.8 on the firmwares (Knulli confirmed) decodes OGG with its bundled
  stb_vorbis, whose type-0 path drops every partition after the first: above 3 kHz the music was
  22 dB too quiet on the RG Cube XX, while the PC (libvorbisfile) was right. Torus Trooper's and
  Mu-cade's tracks were repacked losslessly to type 1; [audio/](../../audio/) has the repacker,
  its verifier and `mixdump` (plays a file through the device's SDL_mixer to a raw file).
- Clipping can be original: Torus Trooper's music is mastered to 0 dB and effects are added at
  full volume, so the mix clips on any platform. A recording of the sink monitor
  (`pw-record --target <sink> -P stream.capture.sink=true`) showed no gaps, only clipping.
- On Knulli SDL picks ALSA, which is PipeWire's ALSA plugin; PipeWire resamples 44100 Hz mono to the
  codec's 48000 Hz stereo.
- **Proprietary middleware has to go.** Stunt Playground replaced FMOD 3.74 with a small mixer on
  SDL2 audio (32 voices, volume, playback frequency for engine pitch, looping).
- OpenAL games: build OpenAL Soft lean and bundle it as `libopenal.so.1` (61 PortMaster ports
  bundle one, Sep 2026).

## Replacing a whole middleware stack

Stunt Playground (2005, CC0 source) was Windows-only on Ogre 1.0, Newton 1.53, CEGUI and FMOD 3.74,
all replaced while keeping the game logic:

| 2005 | Port |
|--|--|
| Ogre 1.0 with its own input | Ogre 14.3.4 (GLES2) in an SDL2 window, input polled from SDL |
| Newton 1.53 + OgreNewt | Bullet, plus a raycast vehicle with Newton's tire model |
| CEGUI | Dear ImGui with images cut from the CEGUI imagesets |
| FMOD 3.74 | own mixer on SDL2 audio |

- When the original physics engine is closed source, run it as the reference: Newton's Linux SDK
  is an i386 static library that links with `gcc -m32`, so a small bench drove the game's vehicles
  in the real engine and the port was tuned against it (lateral g within ~20% on all eight cars).
  The replays shipped with the game gave a second reference (ride heights).
- Match engine defaults, not only the game's settings: Newton bodies default to 0.1 linear and
  angular damping, Bullet to none; Newton auto-freezes resting bodies (piles stay still),
  Bullet needs them started asleep.
- Old scripts and media need conversion to the new engine's syntax (particle scripts, overlays,
  sky boxes, file-name case). Missing manual LOD files crash Ogre: load meshes with
  `removeLodLevels()`.
- Fonts in old media can carry non-free licences: Stunt Playground leaves out the CEGUI fonts.

## First-launch data

- **An original archive shipped untouched** (shareware episode, a media archive whose licence
  demands it unmodified) is unpacked by the launcher on the first launch. Use
  `$controlfolder/7zzs.${DEVICE_ARCH}` (the file PortMaster installs; there is no bare `7zzs`),
  which copes with a DOS stub in front of zip data. Delete the unpacked copy when the user adds the
  full game, carrying save files over (Hocus Pocus).
- **tar on exFAT**: `tar` fails with "Cannot change ownership" (exit 2), so an `&& rm` after it never
  ran and Cube unpacked 30 MB on every launch. Use `tar --no-same-owner`.
- **Game files from a GOG installer** (OpenLoco): innoextract built for aarch64 (Boost, liblzma, bzip2,
  zlib static; 0.98 MB stripped) and run through PortMaster's patcher on the first launch, extracting
  only the needed folders (`-I Data -I ObjData -I Scenarios`). 26 s under qemu, output identical to
  the GOG install. The patcher and Python on the device are covered in [packaging](../packaging.md).
- Demo builds that the engine rejects on purpose (OpenLoco: upstream declined demo support) are not
  worth working around.

## Configuration and saves

- Keep configuration and saves inside the port folder: `XDG_CONFIG_HOME=$GAMEDIR/conf` (OpenLoco),
  `XDG_DATA_HOME` (Domino-Chain), a `HOME`-style variable the game reads (Toppler's
  `TOPPLER_HOME`), or the working directory (Torus Trooper's `tt.prf`, Mu-cade's `mcd.prf`, Cube's
  `config.cfg`). Games that keep their DOS save file next to the data need no
  `bind_directories`.
- Ship defaults the game reads last rather than patching code where the game has a config
  mechanism (Cube's own `autoexec.cfg` with three binds added).
- **Remove every debug and test aid before shipping**: a debug damage key on Enter was found on the
  device in Hocus Pocus.

## Testing native ports

The methods are on [testing and debugging](../testing-and-debugging.md). The ones native ports rely
on most:

- **Frame diffs against the previous build or the original renderer** with scripted input and a
  frozen clock (`replay_compare.sh`, `compare_frames.sh` in
  [devtools/native-testing/](../../devtools/native-testing/)): counts of pixels beyond 1, 2 and 5
  levels plus zoomed crops. The diff caught a lost tunnel slice that no screenshot review would
  have. Keep the unmodified build as the reference picture.
- **Run the aarch64 binary under qemu-user** on the chroot's Mesa. Physics can drift slightly
  between x86_64 and aarch64 (fused multiply-add, libm): Mu-cade's frame 400 showed the same scene
  slightly shifted.
- **Xvfb without a window manager** does not apply SDL fullscreen: letterboxing looks broken until
  openbox runs, unless the game creates its window at the desktop size.
- **The launcher in a fake PortMaster tree** (`pm_sim_test.sh`), with files installed as the real
  installer leaves them.
- **The game's own controller support without a person at the pad**: a virtual uinput gamepad and
  the env that hides the real one ([devtools/device/](../../devtools/device/)). Menus that fade or
  load drop presses: send one action, take a screenshot, then the next.
- **Crashes without gdb**: `segvtrace.c` (LD_PRELOAD) prints a backtrace on fatal signals; resolve
  offsets with `llvm-addr2line -f -C -e <unstripped binary>`. It found Edgar's and Domino-Chain's
  segfaults.
- **Before redeploying a binary**, kill every process whose `/proc/*/exe` is the port binary: a
  leftover instance gave "Text file busy", kept the old binary running and drew over the new one.
- **Profile on the device** before optimising anything (see
  [performance and memory](../performance-and-memory.md)).
