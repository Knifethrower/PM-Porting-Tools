# Graphics

How a game gets its pictures onto a PortMaster handheld: the GL routes that exist (native GLES 2, gl4es,
Westonpack with crusty, glespass, glxsdl), what each costs, the bugs met on the way, filling screens of every
shape, and what the Mali drivers taught. Engine-specific steps (Unity shader and texture conversion, Unity 4
player fixes) are on the engine pages: [Unity](engines/unity.md), [Unity 4](engines/unity-4.md).

Contents: [The routes](#the-routes) · [Native GLES 2](#native-gles-2-when-you-have-the-source) ·
[gl4es](#gl4es) · [Westonpack and crusty](#westonpack-and-crusty) · [glespass](#glespass-gles-games-on-crusty-without-gl4es) ·
[glxsdl](#without-westonpack-glx-on-the-firmwares-sdl2) · [Screen shapes](#filling-every-screen-shape) ·
[Mali lessons](#mali-driver-lessons) · [Debugging](#debugging-a-wrong-picture)

Terms used below:
- **GLES**: OpenGL ES, the only GL the handhelds' GPU drivers offer (GLES 2.0 to 3.2, depending on driver).
- **gl4es**: a library that implements desktop OpenGL (1.x to 2.1, plus ARB programs) on top of GLES 2
  ([ptitSeb/gl4es](https://github.com/ptitSeb/gl4es)).
- **Mali blob / libmali**: Arm's closed GPU driver. **Panfrost**: the open Mesa driver for the same GPUs
  (ROCKNIX ships both on some devices). See [devices and firmwares](devices-and-firmwares.md).
- **Westonpack**: PortMaster's `weston_pkg_0.2` runtime (Weston, Xwayland and the "crusty" GL layers), started
  with `westonwrap.sh`.

## The routes

| Game | Route | Notes |
|--|--|--|
| Source available, draws with OpenGL 1.x/2.x | Native GLES 2 layer compiled into the game | Always preferred. [Native GLES 2](#native-gles-2-when-you-have-the-source) |
| Native aarch64 binary with no GLES path (closed engine, rebuilt source you do not want to touch) | gl4es standalone, through PortMaster's `libgl_default.txt` | Native ports, no Westonpack |
| x86_64 game under [box64](box64.md) that needs X11 + GLX (Unity 5 to 2019 players) | Westonpack `crusty_glx_gl4es`, or `crusty_glx` + glespass once the shaders are GLES | [Westonpack](#westonpack-and-crusty) |
| Unity 4 x86_64 player under box64 | gl4es + an X11 stand-in + GLX on the firmware's SDL2 (glxsdl), no Westonpack | [glxsdl](#without-westonpack-glx-on-the-firmwares-sdl2), [Unity 4](engines/unity-4.md) |
| Program with its own window code (mpv, Ruffle) | Give it an SDL2 window and GLES context instead of its DRM or X11 one | [apps and video](engines/apps-and-video.md), [Windows, Wine and Flash](engines/windows-wine-and-flash.md) |
| ROCKNIX on Panfrost | westonwrap's Mesa bypass: the game runs on the system's desktop GL | [Basics](#basics) |

PortMaster's own description of the Westonpack modes and the gl4es launcher pattern is in its
[porting guide](https://portmaster.games/porting.html); read it first.

## Native GLES 2 when you have the source

**Rule: a game whose renderer can be changed gets a native GLES 2 path, not gl4es.** gl4es is for binaries that
cannot be changed (Unity players, closed engines).

Why, measured:
- gl4es turns every `glBegin`/`glEnd` block and every small `glDrawArrays` into its own GLES draw, and the Mali
  blobs pay a fixed ~30-100 us per draw call. Old immediate-mode games issue 1,000-2,000 draws a frame and crawl.
- Cube on the RG Cube XX (H700, Mali-G31, Knulli), Sep 2026, the same map tour both ways:
  - Through gl4es: 8-22 fps (metl3 22, ruins 11, aard3 13, douze 8, castle 15). Per frame 1,444-1,825 world
    `glDrawArrays` (one per strip) plus ~500 `glBegin` blocks (model strips, one per particle, one per text
    glyph), 49-72 ms to draw it; the main thread spent 61% in gl4es, 27% in libmali, 3% in the game.
  - With its own GLES 2 layer: 60 fps (vsync) on metl3, ruins, aard3 and douze; 19-103 draws and 6.6-9.3 ms per
    frame (castle, whose spawn view looks from outside the map, 14 ms).
- Native also drops gl4es (2.3 MB), GLU and the launcher's libgl block, and removes per-firmware gl4es quirks.

The pattern that worked three times (Torus Trooper, Cube, Bugdom 2): keep the game's GL 1.x calls and implement
them in one file on GLES 2. Load the ES functions with `SDL_GL_GetProcAddress` and link no GL library. Two such
layers are in [gles/](../gles/): a C one with lighting, two texture units, fog, texgen and alpha test (Bugdom 2),
and a C++ one that transforms every vertex on the CPU into one array and draws once per state change (Cube).
Copy the closer one and cut it to what the game uses. How the layers batch, draw wide lines and blend, and the
ES 2 limits a 3D port meets (no depth readback, `gl_FragCoord.z` precision, no line polygon offset):
[native ports](engines/native.md#opengl-1x-games-gl4es-or-an-own-gles-2-layer).

Verify every renderer change with scripted frame dumps against the previous build or the desktop GL build
([native-testing](../devtools/native-testing/)). A pixel diff caught a lost tunnel slice in Torus Trooper that
no screenshot review would have; Bugdom 2 matched its desktop GL build on all 10 levels.

## gl4es

### What it costs

- **Draw calls**: see above. Fine for engines that already batch (Unity), bad for immediate-mode games.
- **Compressed textures**: gl4es advertises only S3TC (DXT) and **decodes DXT on the CPU at upload**, storing
  16-bit colour (4444/565/5551) or 32-bit. Other compressed formats such as ASTC are passed straight to GLES.
  Memory cost, PC measurement of Ittle Dew (Unity 4), Sep 2026: peak RSS 724-733 MB native vs 994-1008 MB
  through gl4es, i.e. ~270 MB for the decode. Mali shares system memory, so this is RAM. On 1 GB devices this
  is the biggest single item; the Unity 4 ports answer it by re-encoding textures as ASTC and relabelling them
  (see [Unity 4](engines/unity-4.md) and [performance and memory](performance-and-memory.md#measure-where-the-memory-goes)).
- **16 or 32 bit**: `LIBGL_AVOID16BITS` defaults to **1** on every GPU except PowerVR (in `src/gl/init.c`), so
  textures are 32-bit unless you set `LIBGL_AVOID16BITS=0`. For Ittle Dew that saved 222 MB of peak RSS on the
  PC and was indistinguishable at 8x zoom on its portraits. Check with an A/B screenshot before paying for 32 bit.
- **Framebuffer copies and `LIBGL_NOBGRA`**, below.

#### Framebuffer copies and LIBGL_NOBGRA

gl4es's `glCopyTexSubImage2D` (in `texture_read.c`) uses the GPU copy only when the bound texture is RGBA8 or
matches `GL_IMPLEMENTATION_COLOR_READ_FORMAT/TYPE`. Anything else becomes `glReadPixels` + `glTexSubImage2D` on
the CPU, with a pipeline stall. Mali advertises BGRA8888, so gl4es keeps Unity's BGRA render and grab textures
as BGRA and every GrabPass or render-texture copy goes through the CPU (the log fills with
`glCopyTexSubImage2D ... GL_BGRA` lines). `LIBGL_NOBGRA=1` keeps those textures RGBA and the copy stays on the
GPU.

**Recommendation: set `LIBGL_NOBGRA=1` on the glxsdl runtime (no Westonpack), on every device.** The Unity 4
ports do. Measured:
- RG Cube XX (H700, libmali, Knulli), Sep 2026: Ittle Dew's world went from a capped 30 to a flat 60 fps with
  the same two copies per frame.
- RG351P (RK3326, ROCKNIX libmali), glxsdl runtime, Oct 2026: 25.0 fps / 72.6 ms CPU per frame with CPU copies,
  33.1-33.5 fps / 55 ms with `LIBGL_NOBGRA=1`; picture correct through intro, title, loading and world.

Caveats:
- **Under Westonpack on the same RG351P** (crusty in Wayland mode, Sep 2026), `LIBGL_NOBGRA=1` gave a black
  world, missing sprites and a black intro. The failing copy was the one from the window surface (Unity's
  per-frame copy for an image effect on a camera that draws to the screen); copies from FBOs were fine, and
  `glFinish`/`glFlush` around it changed nothing. That was most likely the Westonpack surface path rather than
  the driver: the September failures were all under Westonpack, and glxsdl draws on SDL2's own Wayland surface.
  Under Westonpack on RK3326 libmali, leave `LIBGL_NOBGRA` unset (CPU copies, 21 fps there).
- Not re-checked: Ittle Dew's cave fade, and ArkOS/dArkOS on RK3326 (KMSDRM).
- For a driver that gets window-surface copies wrong, glxsdl has an off-screen fallback
  ([below](#without-westonpack-glx-on-the-firmwares-sdl2)), and the 16:9 letterbox avoids window-surface copies
  on non-16:9 screens ([below](#closed-games-a-169-letterbox)).
- Test a GPU-copy switch on every scene and on small sprites (compare crops at the player's position), not only
  on "the world renders": a dialog scene with a big portrait passed a glance while the player sprite was gone.

### Defaults and settings

Check gl4es's defaults in `src/gl/init.c` before passing variables. `LIBGL_ES=2`, `LIBGL_GL=21` and
`LIBGL_SILENTSTUB=1` are already the defaults.

| Variable | What was found |
|--|--|
| `LIBGL_AVOID16BITS=0` | 16-bit textures, less memory (see above) |
| `LIBGL_NOBGRA=1` | GPU framebuffer copies (see above) |
| `LIBGL_SHRINK` | Breaks Unity 4 render targets: modes 1, 2, 5 and 7 render the world black (would have saved 180-300 MB); mode 10 renders but saves only ~40 MB. Shrink textures offline instead |
| `LIBGL_BATCH=1` | Killed Ittle Dew (RG Cube XX, Sep 2026) |
| `LIBGL_MIPMAP=1` | Adds gl4es-generated mipmaps (+33% texture memory); Ittle Dew on the RG353V: ~7 s longer world load and ~95 MB more, sharper sprites at 2:1 minification |
| `LIBGL_BEGINEND=0` | Workaround for the immediate-mode bug below; cost not measured |
| `LIBGL_NOTEST=1` | PC only, and it changes behaviour: see [Testing gl4es on the PC](#testing-gl4es-on-the-pc) |
| `LIBGL_FB` | Set by PortMaster's `libgl_default.txt`: 4, or 2 when `/dev/dri/card0` does not exist |
| `LIBGL_DBGSHADERCONV=1`, `LIBGL_LOGSHADERERROR=1` | Print the ARB-to-GLSL conversion, or the generated GLSL and the compiler error |

Other ports in PortMaster-New tune Westonpack's gl4es with `LIBGL_NOBANNER`, `LIBGL_FORCE16BITS`, `LIBGL_ES=3`,
`LIBGL_MIPMAP`, `LIBGL_ALPHAHACK` and `LIBGL_NOHIGHP` (survey, Sep 2026).

### Known bugs

The Unity 4 ports (Ittle Dew, Teslagrad) ship fixes for the first seven (rows 5-7 since their gl4es source build
of 2026-10-05). The bugs from row 5 on come from the [gl4es test bed](../devtools/gl4es-test/) (findings and
patches in its `FINDINGS.md`) and are reproduced on the PC; their fixes were checked on one device (RG Cube XX:
same picture, same fps, no errors), not in long play.

| Bug | Effect | Fix |
|--|--|--|
| ARB program id recycling: `getUniqueProgramID()` in `oldprogram.c` has `last>upper` instead of `last<upper` | `glGenProgramsARB` reuses freed ids, the program cache returns a GLSL program for deleted ARB programs: SIGSEGV at scene changes in Unity 4 games (Ittle Dew: near the end of the intro) | One comparison; one byte in Westonpack 0.2.7.1's binary |
| `remap_pixel` in `pixel.c` reads `GL_UNSIGNED_SHORT` data as `GLubyte` | 16-bit-per-channel uploads land near zero. Unity 4 uploads its point-light attenuation texture as `GL_ALPHA16`, so every point light vanishes (Teslagrad's menu highlight) | `GLushort` (one word); 8 bytes in the shipped binary. Still on master as of Oct 2026 |
| `createMainFBO` makes separate `STENCIL_INDEX8` + `DEPTH_COMPONENT24` renderbuffers | Mali rejects that combination, gl4es deletes the FBO: black screen when the main FBO is used | One packed `DEPTH24_STENCIL8` for both attachments (3 bytes) |
| Westonpack's `gl4es_glxpass` build prints a debug line on every `glReadPixels` and `glCopyTexSubImage2D` | ~3000 log lines a minute for a game with a grab pass; no variable turns it off | NOP the two calls, or filter the log |
| ARB parser `resize()` stores bytes as an element count | Heap overflow on longer ARB programs; 5 Unity programs trigger it (3 Unity built-ins, 2 of Teslagrad's), and Teslagrad's real call stream aborts in `glProgramStringARB` on the PC. On the device: possible random crashes later | Fixed in the ports' source build (2026-10-05); short device A/B only |
| More than 24 fragment-program locals overflow `frg_progloc[24]` | Unity's screen-space shadow collector uses 27 | Fixed in the ports' source build (2026-10-05); short device A/B only |
| `glBindProgramARB` and the program parameter setters do not flush merged `glBegin`/`glEnd` blocks | An immediate-mode quad drawn with program A renders with program B or later parameters (Unity's `GL.Begin`: Blit, GUI, image effects) | Fixed in the ports' source build (2026-10-05); short device A/B only. Otherwise `LIBGL_BEGINEND=0` |
| `OPTION ARB_fragment_program_shadow` rejected, and drawing with a rejected ARB program crashes | 42 Unity shadow-receiving programs fail to load; whether a game reaches them depends on shadows being on | Not fixed |

DXT decoded to 16 bit gives slightly softer icons (max diff 71 on one HUD button in the Ittle Dew replay): a known
trade-off, not a bug. Report gl4es bugs upstream with a reproducer; the test bed has one for each of these.

### Where ports get gl4es

- **Native ports (no Westonpack)** ship their own `gl4es.aarch64/libGL.so.1` + `libEGL.so.1` and source
  PortMaster's `libgl_default.txt` (or `libgl_<CFW>.txt`) after `cd` into the game folder. At least 37 ports do
  this (Aquaria, Quake 3, Xmoto, ...). Ports point SDL at the files with `SDL_VIDEO_GL_DRIVER` /
  `SDL_VIDEO_EGL_DRIVER`. The Nov 12 2024 build shared by a dozen ports is a GBM build and cannot draw on X11:
  test the GL translation on the PC with an x86_64 gl4es, and the aarch64 binary separately.
- **Westonpack `crusty_glx_gl4es`** always uses Westonpack's own `gl4es_glxpass/libGL.so.1`: westonwrap puts it
  first in `gllib_path`, which precedes `WRAPPED_LIBRARY_PATH`. The banner `LIBGL: v1.1.7 built on Mar 10 2025`
  in the log is that file. Survey of PortMaster-New, Sep 2026: 22 ports use this mode and none ships or preloads
  its own gl4es. Westonpack also carries `gl4es_glxpass_debug`, `gl4es_noloader`, `gl4es_x11` and
  `core4es_glxpass` builds.
- **A different gl4es under Westonpack**, two ways: (a) keep `crusty_glx_gl4es` and preload your copy with
  `WRAPPED_PRELOAD_MALI="$GAMEDIR/gl4es.aarch64/libGL.so.1"`. crusty `dlopen`s gl4es by its soname, so it gets
  the preloaded copy and there is one gl4es in the process. `WRAPPED_PRELOAD_MALI` is unset in the ROCKNIX
  Panfrost bypass, where gl4es is not used. (b) the Warzone 2100 pattern: `crusty_x11egl` (adds no libGL) with
  your own libGL + libEGL on `WRAPPED_LIBRARY_PATH`. (b) swaps the whole GLX path and is untested with Unity.
  A stock gl4es cross build does not run under crusty (its `gl4es_glxpass` is a modified build), so byte patches
  on the shipped library were the practical way to change gl4es there.
- **System gl4es**: ROCKNIX's build recipes have a gl4es package for `/usr/lib/gl4es/libGL.so.1`, but the RK3326
  image of 2026-09-01 does not contain it (`/usr/lib/libGL.so.1` there is a 0-byte stub, the cause of "file too
  short" log lines). muOS ships one in `/usr/lib/gl4es`; westonwrap ignores it. Check that the file exists and
  is a non-empty ELF before relying on it.
- **Built from source**: the Unity 4 ports now build gl4es a744af14 (the base of Westonpack's v1.1.7) for
  aarch64 in the bullseye chroot, with their fixes and a small `glxpass.c` that forwards `glX*` to glxsdl. Its
  exports match the Westonpack binary, and on the RG Cube XX (Oct 2026) it ran Ittle Dew and Teslagrad with the
  same picture, fps and memory as the patched binary. Recipe: [gl4es-test](../devtools/gl4es-test/).

Shipping a patched binary: put it pre-patched in `<port>/gl4es.aarch64/libGL.so.1` (the folder name PortMaster's
guide uses), include `LICENSE.gl4es.txt` (MIT), and give the exact patch commands in the README's Compile section.
Patching a byte fix into a runtime's gl4es on every launch cost about 20 launcher lines and was rejected as too
much.

### Testing gl4es on the PC

- x86_64 gl4es on Mesa GLES 2 inside a Unity 4 process needs `LIBGL_NOTEST=1` (otherwise Mesa's LLVM
  initialises before Unity's allocator is ready and crashes). **In no-test mode gl4es assumes almost no
  capabilities**: limited NPOT, 2048 max texture, no depth24 / packed depth-stencil / depth textures, and
  `maxcolorattach` = 0, so every `glFramebufferTexture2D(GL_COLOR_ATTACHMENT0)` is rejected. The FBO is
  depth-only yet "complete" and anything rendered to a render texture is lost (Ittle Dew on PC: black world,
  HUD fine, magenta grab-pass shaders).
- The real Mali-G52 reports full NPOT, depth24, packed depth-stencil, depth/stencil textures, 16K textures and
  one colour attachment. [gl4es-mali](../devtools/pc-testing/gl4es-mali/) is gl4es with a profile
  (`GL4ES_MALI_PROFILE=1`) that sets those values in no-test mode, including BGRA8888, so PC tests take the same
  copy path as the device. With it Ittle Dew rendered identically to native. **Do not chase a gl4es rendering
  bug on the PC before testing with this profile.**
- The tell for a missing colour attachment: `GL_IMPLEMENTATION_COLOR_READ_FORMAT` = 0, and plain RGBA
  `glReadPixels` fails with `GL_INVALID_OPERATION` and reads zeros.
- [gl4es-test](../devtools/gl4es-test/) runs the same GL work on Mesa desktop GL and through gl4es with the Mali
  profile: ~13,000 differential tests, every ARB program the Unity 4 games ship, piglit, apitrace replays of real
  games, AddressSanitizer. llvmpipe is not the Mali blob: a finding still needs a device check.
- AddressSanitizer does not work inside the Unity player (its own `operator new` crashes under ASan). To find a
  use-after-free there, add a small trace: record freed objects, check pointers at the use site. That is how the
  ARB id bug was found, with `addr2line` on a debug build.

## Westonpack and crusty

### Basics

- Launch: `westonwrap.sh headless noop kiosk crusty_glx_gl4es <env assignments...> <command>`. The mode table
  is in PortMaster's guide.
- **`$ESUDO` (sudo) drops exported variables.** Pass every setting as an `env VAR=value` argument on the
  westonwrap line; exports are overridden inside westonwrap anyway.
- `WRAPPED_LIBRARY_PATH` / `WRAPPED_PRELOAD` always apply; the `..._MALI` variants only when crusty is used
  (dArkOS, ArkOS, ROCKNIX on libmali); `..._PANFROST` only in the ROCKNIX Panfrost bypass. Preload order:
  `WRAPPED_PRELOAD : _MALI : _PANFROST : crusty`. `gllib_path` comes before `WRAPPED_LIBRARY_PATH` in
  `LD_LIBRARY_PATH`, so a library in your libs folder does not replace one Westonpack brings.
- **Never put a `libGL.so.1` in a folder on `WRAPPED_LIBRARY_PATH`**: the Panfrost bypass would load it
  instead of Mesa.
- ROCKNIX detection inside westonwrap: `glxinfo | grep "OpenGL version string"` succeeds means Panfrost/Mesa,
  and westonwrap **bypasses** Weston and runs the command directly on ROCKNIX's desktop with native Mesa GL
  (unless `NO_PANFROST_BYPASS=1`). It fails on libmali, and crusty runs in Wayland mode (`CRUSTY_WLMODE=1`).
  In the bypass only `WRAPPED_LIBRARY_PATH*` reaches the game, not your `LD_LIBRARY_PATH`.
- What works where for Unity 5 to 2019 games on crusty + glespass (dArkOS, ROCKNIX libmali, ROCKNIX Panfrost):
  [Unity: status per firmware](engines/unity.md#status-per-firmware).
- crusty variables (from the binary): `CRUSTY_BLOCK_INPUT`, `CRUSTY_SHOW_CURSOR` (cursor is off unless 1),
  `CRUSTY_FPS`, `CRUSTY_RESOLUTION`, `CRUSTY_GLES`, `CRUSTY_GL4ES` (crusty initialises gl4es only with it;
  westonwrap sets it), `CRUSTY_LIBEGL`, `CRUSTY_LIBSDL`, `CRUSTY_WLMODE`, `CRUSTY_WLDISP`.
  `CRUSTY_BLOCK_INPUT=1` is the one a typical port needs.
- **`CRUSTY_FPS=1`** prints `[CRUSTY] FPS: %.2f` to the game's stdout: a free on-device frame-rate log (it ends
  up in Unity's log). It printed nothing on the TrimUI Brick (A133P); the x86 preload
  [fpslog](../devtools/profiling/fpslog/) works everywhere.
- The source of truth is the runtime itself: unpack `weston_pkg_0.2.aarch64.squashfs` (PortMaster-New
  `runtimes/`) and read `westonwrap.sh`.
- Cost: Weston ~12 MB and Xwayland ~30 MB RSS next to the game (RG351P, Sep 2026), and a 55 MB runtime
  download. A libX11 stand-in that answers the game's Xlib calls in-process keeps Weston from starting
  Xwayland at all (-15 to -19 MB on the RG351P, same frame rate): [xstub](../shim/xstub/).

### What crusty does not do

- **`glXMakeCurrent` is bookkeeping only.** crusty's single EGL context stays current on the main thread
  forever. A game that renders on a separate thread (Unity's threaded renderer) "binds" it there and draws into
  nothing: shaders get id 0, link failures, black screen. Render on the main thread (Unity `-force-gfx-st`) or
  hand the context over for real (glespass `GLESPASS_CTXFIX`, below); the gain is in
  [Unity: threads and the GL context](engines/unity.md#threads-and-the-gl-context).
- **Nothing is cleared outside a viewport.** A game that letterboxes by drawing into a smaller camera rect shows
  stale frames in the bars on Westonpack (on the PC under Mesa/X they stay black). Something has to clear the
  full window every frame; for Unity 4, a clearing camera ([Unity 4](engines/unity-4.md)).
- **No video-memory query extensions** (`GL_NVX_gpu_memory_info`, `GL_ATI_meminfo`, `GLX_MESA_query_renderer`):
  neither gl4es nor crusty offers one. Unity 4 then assumes 64 MB of VRAM and shrinks big textures, which render
  as stripes; the player fix is on the [Unity 4](engines/unity-4.md) page.

## glespass: GLES games on crusty without gl4es

For a game that already issues only GLES calls (a Unity 5 to 2019 build whose shaders were converted to GLES 3,
see [Unity](engines/unity.md)), gl4es has nothing to translate. crusty still expects a gl4es: it calls
`initialize_gl4es()` / `gl4es_GetProcAddress()` and wants all `gl*` entry points. glespass
([shim/glespass-nitw](../shim/glespass-nitw/), [shim/glespass-gonehome](../shim/glespass-gonehome/), tests in
[shim/glespass-test](../shim/glespass-test/)) is a `libGL.so.1` that exports gl4es's symbol list (1,222 gl + 40
glX names) as aarch64 trampolines bound at load time:
- gl names: a symbol inside libcrusty if crusty hooks it, else `dlsym` in `libGLESv2`, else
  `eglGetProcAddress`, else the name with its ARB/EXT/OES suffix stripped, else a no-op returning 0.
- glX names: crusty's implementation (plain `glXGetProcAddress` maps to `...ARB`).

Lessons from building it:
- Preload it **ahead of crusty** (`WRAPPED_PRELOAD_MALI`) so the game's lookups pass through it, and wrap
  `glXGetProcAddress` so later lookups are hooked too.
- **Do not link it against `libcrusty.so` or `libGLESv2`**; `dlopen` them with `RTLD_NOLOAD` at run time.
  westonwrap preloads it into helper commands too (`pkill` etc.), which fail otherwise.
- crusty's getproc can return **your own trampoline** (global scope). Wrapping it again made the wrapper call
  itself: a stack overflow at startup with no logs. Never wrap anything whose `dladdr` base is your own library.
- **Threaded rendering fix** (`GLESPASS_CTXFIX=1`): wrap `glXMakeCurrent` / `glXMakeContextCurrent` and hand the
  context over with `SDL_GL_MakeCurrent` on crusty's window and context, captured with
  `SDL_GL_GetCurrentWindow/Context` on the main thread (SDL found through `CRUSTY_LIBSDL`). Why a raw
  `eglMakeCurrent` is not enough, and the frame rates: [Unity](engines/unity.md#threads-and-the-gl-context).
- Diagnostics (environment variables): `GLESPASS_GLXTRACE=1` + `GLESPASS_TRACEFILE=path` (context
  create/bind/destroy per thread with the EGL state after each call, first GL calls per thread, pixel samples
  and full-frame PPM dumps at fixed frames, `glGetError`), `GLESPASS_SHADERLOG=N` (first N compile/link failures
  with source), `GLESPASS_FPSFILE` (fps counted at swap), `GLESPASS_RENDERTID` (writes the swapping thread's id,
  for thread pinning), `GLESPASS_OPAQUE` (alpha = 1 before swap). Write diagnostics from an injected library to
  its own file (`O_APPEND`, one `write()` per line): Unity's `-logFile` takes over stderr and overwrites other
  writers.
- It was developed against fakes of GLES, EGL, SDL and crusty (per-thread current context and window), so the
  handoff logic was tested on the PC before the device.

The log line `431/1222 gl, 40/40 glX bound` in the launcher log is glespass's binding summary.

## Without Westonpack: GLX on the firmware's SDL2

An x86 GL game under box64 needs three things from the system: an X11 display (window, properties, keyboard),
GLX (a context) and the GL calls. Westonpack provides the first two with Weston + Xwayland + crusty. All three
can be provided in-process instead, with box64 and gl4es unchanged ([shim/xstub](../shim/xstub/)):
- **xstub**: a `libX11.so.6` stand-in with real Xlib struct layouts that answers the game's Xlib calls locally:
  one screen, an EWMH window-manager face, the event order Xwayland sends, keyboard from evdev.
- **glxsdl**: the `crusty_glX*` symbols the pass-through gl4es binds to, implemented on the firmware's SDL2
  (`dlopen`ed by name): one fullscreen window, one GLES 2 context, `initialize_gl4es()` right after the context,
  `gl4es_pre_swap`/`gl4es_post_swap` around `SDL_GL_SwapWindow`.
- Stand-ins for the small libraries the game links (libGLU, libXcursor), and a box64 patch so missing helper
  libraries of a wrapped library are not fatal ([box64](box64.md)).

The portability argument: SDL2 picks the display path like any native port (Knulli: `mali` fbdev, ROCKNIX:
Wayland under Sway, KMSDRM elsewhere). Knulli's SDL2 has only the `mali` and `dummy` video drivers, so a program
that wants DRM directly fails there.

Measured with Ittle Dew (Unity 4), Sep 2026: RG Cube XX (Knulli) 60 fps and +20 MB free; RG351P (ROCKNIX) 31 fps
(same as Westonpack) and +35 MB free; no 55 MB runtime download; zip +0.3 MB. The launcher line becomes
`LD_LIBRARY_PATH=$GAMEDIR/libs.aarch64 LD_PRELOAD=<gl4es>:libglxsdl.so` around the box64 command, with
`"runtime": []` in port.json. Unity 4 ports now always use this route ([Unity 4](engines/unity-4.md)).

Traps:
- Hosts in crusty's role must call `initialize_gl4es()` themselves; the first `glClear` crashes otherwise.
- box64's libX11 wrapper dereferences `_XLockMutex_fn` / `_XUnlockMutex_fn` at load and reads `struct _XDisplay`
  fields: export them and keep the real Xlib struct layouts.
- crusty and gl4es hook libc file I/O in the process: read evdev with raw syscalls. glibc's `syscall()` returns
  -1 and sets errno.
- Virtual keyboards reuse node names (check liveness); opening evdev nodes takes 70-135 ms on the H700 kernel, so
  do not rescan rejected nodes every time (it showed as a 60/48 fps alternation).
- Some firmware SDL2 builds install crash handlers that replace box64's: see [box64](box64.md#signals-and-native-libraries-in-the-box64-process).
- Do not keep a fullscreen window smaller than the desktop the game is told about: Unity 4 re-requested
  fullscreen every ~2.5 s (choppy loading, dips). Report the game's size as the desktop instead.
- Stand-ins need only what the game imports: `nm -D --undefined-only <player>` per library, plus a grep of the
  player's strings for names looked up with `dlsym`. Check with `comm -23 <imports> <exports>`.
- Xlib property format 32 means `long` (8 bytes on aarch64), not 4.
- To learn what a game expects from the X server, record a normal run with Westonpack's `tools/xtrace` (run the
  proxy outside the crusty preload).

`glxsdl-offscreen.patch` in the same folder renders into a window-sized FBO made through gl4es and draws it to
the window at swap: correct on the RG351P at ~2 ms per frame. It is for a driver that gets window-surface copies
wrong; the 16:9 letterbox below makes it unnecessary on non-16:9 screens.

The same idea, an SDL2 window and context instead of the program's own display code, works for programs that
are not games: mpv ([apps and video](engines/apps-and-video.md)) and Ruffle
([Windows, Wine and Flash](engines/windows-wine-and-flash.md)).

## Filling every screen shape

Screens in use include 640x480, 720x720, 480x320, 854x480, 960x544, 1024x768, 1280x720, 1440x1080 and 1920x1152.
No PortMaster handheld is wider than 16:9 (as of Sep 2026).

### Games with source: extend the view, never letterbox

Keep the original view inside and extend it (Hor+ on wider screens, Vert+ on narrower ones), widen the 2D
overlay by a margin, and give culling and camera clamps the angle actually shown. Mu-cade, Cube, Torus Trooper
and Bugdom 2 fill every shape this way; the method and its tests are on
[native ports](engines/native.md#filling-every-screen-shape). Engine-specific helpers: [LÖVE](engines/love.md)
(`fit.lua`), SDL2 renderer games (`SDL_RenderSetLogicalSize`).

### Closed games: a 16:9 letterbox

For a binary that cannot be changed, the cleanest result is the game's own design aspect (usually 16:9) as large
as the screen allows, with black bars:
- **Read the game's own letterbox code first.** Ittle Dew renders 16:9 and sets its camera rect to
  `y = n/2, h = 1-n` with `n = 1 - screenAspect/(16/9)`: correct on every shape up to 16:9 once something clears
  the bars. Only screens wider than 16:9 give `n < 0`.
- Perspective cameras keep the vertical view, so on narrower screens the sides are lost (Teslagrad on 1:1 lost
  ~22% of each room per side).
- **Present a 16:9 frame through gl4es's main FBO** (Teslagrad, Oct 2026): tell the game its desktop is the 16:9
  fit of the screen (xstub `XSTUB_SCREEN=WxH`, computed by the launcher; pixel-exact on every common size), and
  let glxsdl present gl4es's main FBO centred. Once the game's window is smaller than the SDL window, glxsdl calls
  `createMainFBO(w, h)` (gl4es then routes framebuffer 0 and its reads into it); per swap `unbindMainFBO`, black
  clear, `blitMainFBO(x, y, w, h)`, swap, `bindMainFBO`, clear colour/depth/stencil as for a fresh back buffer.
  These four are local symbols in gl4es, found by name in the shipped file's `.symtab`. Needs the packed
  depth-stencil fix above. Result on the RG Cube XX: 720x405 at 50-60 fps where 720x720 ran 20-30. Side effect:
  the game always renders into an FBO on non-16:9 screens, so no copies of the window surface happen there.
- The Unity 4 details (the player stretches when its resolution differs from the desktop, its built-in
  resolution list has no small handheld sizes, scene-level fixes for menus) are on the
  [Unity 4](engines/unity-4.md) page.

## Mali driver lessons

From the Mali blob (libmali) on RK3566 (Mali-G52), RK3326 and H700 (Mali-G31):

- **Draw-call cost is fixed per draw**, ~30-100 us, which is what makes gl4es slow for immediate-mode games. But
  do not tune draw calls on a guess: in Torus Trooper (Mali-G31, fbdev, r20p0) one draw of 40-70 thousand
  vertices cost 1-3 ms and the swap under 1 ms; the GPU was never the limit once the game's maths was fixed.
- **Never stream vertex data into one VBO with `glBufferSubData`** while earlier draws still use it: the blob
  copies the whole buffer for every update. 1 MB x 300 draws per frame gave 1.3 s frames (0.8 fps). Mesa does
  not do this, so the PC cannot show it. Client-side arrays, or one upload per frame, are safe. Orphaning
  (`glBufferData(NULL)`) must not fall between two uploads of the same draw: reserve everything a draw needs at
  once.
- **A GPU-to-CPU readback costs ~10 ms on the Mali-G31 whatever its size** (fence latency, waiting in
  `osup_sync_object_timedwait`; Oct 2026). CPU clocks and the GPU governor do not change it. Count readbacks and
  remove what forces them rather than making them faster.
- **No S3TC/DXT**; ASTC and ETC2 are supported. Desktop games' DXT textures must be decoded (gl4es does it at
  upload, in RAM) or converted offline.
- **BGRA8888 is advertised**, which sends gl4es's framebuffer copies through the CPU:
  [LIBGL_NOBGRA](#framebuffer-copies-and-libgl_nobgra).
- **GLES only on the blob**: desktop-GL-only games need gl4es or shader conversion (Unity: GLCore to GLES 3, see
  [Unity](engines/unity.md)). Panfrost offers desktop GL 3.1 through Mesa.
- **Separate depth and stencil renderbuffers on one FBO are rejected**: use packed `DEPTH24_STENCIL8`.
- **The GLES back buffer is not kept between presents**: a game that presents only dirty rectangles shows partial
  frames; redraw or copy the whole frame each present.
- **No hardware cursor on KMSDRM/mali**: games that use `SDL_CreateColorCursor` show no pointer; draw the cursor
  into the frame.
- **Window-surface copies on RK3326 libmali under Westonpack returned wrong content** (Sep 2026): a GPU
  `glCopyTexSubImage2D` from the default framebuffer gave black or stale pictures regardless of
  `glFinish`/`glFlush`, while copies from FBOs were fine. On the same device through SDL2's own Wayland surface
  (glxsdl) it was correct. Test every libmali generation and window system separately.
- The 30 fps many ports report on these devices is often a **vsync step**, not the game's speed: on the KMS flip
  path any frame over 16.7 ms lands on 30. Compare variants by CPU time per frame
  ([performance and memory](performance-and-memory.md)).

Panfrost vs libmali per firmware: [devices and firmwares](devices-and-firmwares.md).

## Debugging a wrong picture

- **Reproduce on the PC with the device's capabilities** (gl4es-mali profile, or a GLES context on Mesa) before
  touching the device. See [testing and debugging](testing-and-debugging.md).
- **Trace one frame end to end.** Log the GL calls the game gets through `glXGetProcAddress` (in the GLX layer)
  and the ones it imports directly (an x86 preload under box64, e.g. [fpslog](../devtools/profiling/fpslog/)),
  both appending to one file while a trigger file exists. Teslagrad's paused frame drew 2 quads instead of 12: a
  CPU-side problem, not a GL one. Note that a Unity 4 player imports `glTexImage2D` and the rest of GL 1.x
  directly and asks `glXGetProcAddress` only for extensions (FBO, compressed textures), so a GLX-layer hook
  cannot see texture uploads.
- **Count copies**: fpslog prints fps, the longest frame, grabs per second and their size, and with
  `FPSLOG_TEX=1` each distinct texture upload shape once.
- **Look at sequences and crops, not single screenshots.** A screenshot every 3 s from the first scene load and
  fixed-position crops compared side by side found black intro pages and a missing player sprite that single
  shots missed. On Knulli `cat /dev/fb0` gives the scanout (2 pages); on ROCKNIX use `grim` with the session's
  Wayland variables.
- A shader that works on Mesa and on the PC gl4es can still fail on the device: a Unity 4 build with raw GLSL
  shaders sampled opaque black on the RG353V for every texture not uploaded in the current frame; Cg shaders
  (the ARB path real games use) fixed it ([Unity 4](engines/unity-4.md)).
- gl4es debug output (stdout) lands in the game's log; Westonpack's build prints on every copy (see the bug
  table), so filter the log or use a build without the prints.
