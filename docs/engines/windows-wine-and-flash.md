# Windows games: Wine, and Flash / Adobe AIR through Ruffle

Two routes for games that exist only as Windows builds: a Box64 + Wine runtime for real Windows
programs, and Ruffle for Flash and Adobe AIR games (which are SWF files inside a Windows wrapper).
Both are newer and less proven than the other engine pages; status is given per item, as of Oct 2026.
Tools: [wine/](../../wine/) ([README](../../wine/README.md)).

## Try the other routes first

A Windows `.exe` is the last resort, not the first:

1. A native Linux x86_64 build of the same game through box64 ([box64](../box64.md)) is always the
   better route than Wine.
2. Windows-only **Unity** games with Mono scripting: the game's data on a Unity Linux player of the
   same version ([Unity 4](unity-4.md)).
3. Windows-only **Godot** or **GameMaker** games: their pack or `data.win` runs on the engine's own
   Linux/Android route ([Godot](godot.md), [GameMaker](gamemaker.md)).
4. **Flash / AIR**: Ruffle (below), not Wine.
5. Anything else: Wine.

## The Box64 + Wine runtime

Status: a working runtime squashfs (`wine_box64_11.0`) and a test port, built by the author; **not a
PortMaster runtime** and not hosted by PortMaster (Oct 2026). 2D programs (GDI, DirectDraw) run on
the device (RG353V, dArkOS); xbridge's picture is verified on the PC, a device confirmation is
still pending. Direct3D does not work yet. No game ships on it.

### Design

- **Wine 11.0 in WoW64 mode** (a prebuilt amd64 WoW64 build): 32-bit Windows programs run inside the
  64-bit Wine, so box64 alone is enough. No box86, no armhf libraries on the device. Checked under
  qemu-aarch64: 32-bit `cmd.exe` reports `x86` with `PROCESSOR_ARCHITEW6432=AMD64`.
- **One runtime squashfs mounted by every port**, next to Westonpack, instead of per-port copies.
  Trimmed (headers, import libraries, build tools, man pages): 693 -> 531 MB. Stripping PE debug info
  saved only ~3 %.
- **Compress the squashfs with gzip, not xz.** dArkOS's kernel (RG353V) refused to mount the xz
  image ("Filesystem uses xz compression. This is not supported"). gzip -9: 155 MB.
- Wine's unix side dlopens X11, freetype, fontconfig, GL/EGL; box64 wraps them to native aarch64
  libraries, which Westonpack's library folder mostly provides. Not covered: gnutls (TLS), gstreamer
  (Media Foundation video).
- `WINEDLLOVERRIDES=mscoree,mshtml=` (no Mono or Gecko shipped) and `winedbg` / `winemenubuilder`
  disabled: a crashing installer otherwise left Wine's crash debugger waiting forever.

### The prefix

A Wine prefix needs symlinks and must be owned by the user running Wine. FAT and exFAT cards have
neither ("... is not owned by you" on the RG353V). The runner script uses the port folder when a
test (mkdir, ownership, `ln -s`) passes, else an **ext4 image loop-mounted** over the prefix folder
for the run (256 MB default, `mkfs.ext4 -m 0`), else a folder in the home directory.

- **Ship a ready-made empty prefix**, not `wineboot` on the device: `wineboot -i` took 2 min 25 s
  under qemu; unpacking a prefix and running a 32-bit `cmd` took 55 s, and on the PC the first-run
  overhead was 8 s.
- [link_prefix.py](../../wine/) replaces every prefix file that is byte-identical to a Wine builtin
  with a relative symlink into the runtime: 1532 files (539 MB) became links, the prefix tar is
  26 MB. Caveat: a Wine version change runs `wineboot -u`, which would write through those links
  into the read-only runtime. A new Wine version needs a new prefix.
- Run `wineserver -w` after the program so the prefix image and the squashfs can be unmounted.
  Avoid `sync` in scripts (it hung for minutes in WSL; unmounting flushes the image anyway).
- Without a window manager, `start /max` does nothing. **Wine's virtual desktop**
  (`explorer /desktop=name,WxH start /wait /max prog`) maximizes by itself and absorbs resolution
  changes: use it for game ports.

### Getting 2D programs on screen: xbridge

Westonpack's headless Weston shows only OpenGL programs (through crusty). Wine draws GDI and
software DirectDraw with plain X11 into Westonpack's rootless Xwayland, and with the `noop`
renderer that ends up nowhere: the screen stayed black while the tests ran fine.

[xbridge](../../wine/xbridge/) copies the visible top-level windows of that Xwayland (XShm, stacking
order, cursor) to the screen through the firmware's SDL2, scaled and vsynced. It also grabs
gptokeyb's virtual keyboard/mouse device and injects its events with XTest: without that, gptokeyb's
keys and buttons never reached Wine (the stick moved the X pointer, touch worked, nothing else).
Westonpack and its modes: [graphics](../graphics.md).

Status (Oct 2026): 2D programs (GDI, DirectDraw) run on the device (RG353V, dArkOS); xbridge's
picture is verified on the PC, a device confirmation is still pending.

Measured on the RG353V (dArkOS, Sep 2026) with the [test programs](../../wine/tests/), 32-bit,
in the run that showed the black screen (the programs log their own frame rates):

| Test | Result |
|--|--|
| GDI at 640x480 | 130 fps |
| DirectDraw 8-bit palette flip / 16-bit | 64.8 / 88.6 fps |
| DirectDraw windowed Blt | 154 fps |

DirectDraw needs `WINE_D3D_CONFIG=renderer=no3d` here (wined3d's software path); otherwise wined3d
reaches for OpenGL (below).

Things that went wrong on the way and apply to any Westonpack launcher:

- Westonpack's `drm` mode failed on dArkOS (RG353V) with "failed to create compositor" (another
  merged port failed the same way). `headless noop kiosk` + xbridge works.
- `crusty_glx` is preloaded into every process Westonpack starts and prints a banner to stdout, so
  `$(dirname ...)` and any other captured command output got the banner. Use `${0%/*}` and avoid
  capturing external commands' output in scripts run under Westonpack.
- box64 renames its processes, so a gptokeyb watching "box64" never fires. Watch `wineserver`, or a
  dedicated marker process, and on quit run `wineserver -k` (killing only wineserver left the
  program hanging).
- gptokeyb2's `mouse_scale` is a speed multiplier (higher = faster, default 512): 2048 moved the
  pointer ~2000 px per tick. Explained on [Unity 4: Input](unity-4.md#input).

### Direct3D: open

wined3d turns Direct3D 8/9 into OpenGL, and whatever GL Wine uses must draw **into the X window**,
because xbridge only sees X windows. Status of the attempts (Sep-Oct 2026, none working on a
device yet):

- **Wine 11's X11 driver uses EGL by default**, which on the device reached the Mali blob and drew
  off-screen. Set `UseEGL=N` (registry, `HKCU\Software\Wine\X11 Driver`) to use GLX.
- **virgl through Mesapack** (`mesa_pkg_0.1`): works on the PC (Xvfb, 187-204 fps on the D3D9 test),
  fails on the device ("Failed to make caps GL context current"). Found so far: Mesa's GLX needs six
  libxcb libraries (glx, dri2, dri3, present, randr, sync) that are in neither Mesapack nor
  Westonpack; the runtime now ships them, and the caps GL context still failed on the next device
  run. box64 must also resolve Mesa's libGL, not the system's.
- **gl4es through crusty_glx_gl4es**: Wine GLX -> gl4es -> GLES straight on Mali. Set up, untested;
  it draws to the screen itself, so it cannot be mixed with xbridge's 2D.
- Later: DXVK on Vulkan (Panfrost/PanVK firmwares only).

Peggle Deluxe (a PopCap disc game) is blocked on this: even with its 3D option off it needs
wined3d's OpenGL (`renderer=no3d` leaves the screen white).

### Programs with a fixed image base

Programs without relocations must be mapped at their base address. box64 0.4.4 reserves only part
of the low 32-bit area for Wine, and a program whose fixed image reaches past 0x30110000 (Word
Viewer 2003: 0x30000000-0x30881000, no relocations) failed with
"failed to create main module ... c0000018". The [box64 patch](../../wine/box64/) reserves up to
box64's own load address, as wine-preloader does.

### Installing from the user's disc

- [bin2iso.py](../../wine/) turns a one-track MODE1/2352 `.bin` into an `.iso`. Mount it and make it
  Wine drive D: as `cdrom` (registry `HKLM\Software\Wine\Drives` plus the `dosdevices` links).
- Some disc games refuse to start from copied files and must be installed with their own installer
  (Peggle Deluxe). Run the installer under Wine with the user's disc image; after a normal install
  it ran without the disc on the PC. Whether such an install still works when moved to a device
  (another CPU under box64) is not known.
- `msiexec` page-faulted under box64 on the device while it worked natively; installing on a PC
  with the same runtime is the fallback for personal use. A prefix with a game installed in it
  contains the game: it can never be part of a published port.

### Testing without a device

- The [test programs](../../wine/tests/) (GDI, DirectDraw, Direct3D 9; 32-bit, built with mingw)
  log every call's result and count input into `results.txt`.
- The runtime's runner script works under qemu-aarch64 in WSL; Wine natively in Xvfb checks the
  same prefix and xbridge (`XBRIDGE_DUMP` writes a frame, `XBRIDGE_NOVIDEO` + `XBRIDGE_INPUT` with
  [send_events.py](../../wine/) test input). Without a `DISPLAY` Wine's explorer never exits and
  `wineserver -w` waits forever.
- [glxcheck](../../wine/glxcheck/) tells whether native OpenGL works on a display, without box64
  or Wine.
- For testers: PortMaster's autoinstall folder takes loose `.squashfs` files and moves them into its
  libraries before installing port zips, so a test runtime can be installed without hosting.

More on box64 itself: [box64](../box64.md).

## Flash and Adobe AIR games: Ruffle

Steam "PC" releases of old Flash games are often **Adobe AIR apps**: a `.swf`, a `META-INF/AIR`
folder and an `Adobe AIR/` runtime folder, sometimes with native extensions (ANEs). The SWF is the
whole game; the route is [Ruffle](https://ruffle.rs/) (a Flash player in Rust) built for aarch64,
not Wine.

Two ports so far, neither in PortMaster yet (Oct 2026):

- **Fancy Pants Adventures: Classic Pack** by crxssrazr93
  ([fancypants-portmaster](https://github.com/crxssrazr93/fancypants-portmaster), MIT): desktop
  Ruffle with four patches on Westonpack, game SWFs patched on the device with xdelta, Stage3D ATF
  textures converted to half-size PNGs, Ruffle restarted per world so one world is in memory at a
  time. Their numbers (RG35XX H): worlds 1-3 at 30 fps, the Stage3D world 4 hub ~15 fps (CPU bound),
  one OOM kill at 700-790 MB during a world 3 level load. A variant on the author's SDL front end
  (below) is in progress.
- **Offspring Fling** (AIR 3.1, FlashPunk, obfuscated): the author's port on an own Ruffle fork with
  an SDL2 front end. 60 of 60 game updates/s on the RG Cube XX.

### What AIR adds, and Ruffle lacks

Ruffle runs AIR SWFs with `--player-runtime air`, but AIR's own APIs were stubs. The author's fork
(not public; described by what it adds) has:

- `flash.filesystem` File / FileStream / FileMode on Ruffle's storage backend (app storage only:
  files land as `<save dir>/air/<path>.sol`, so a launcher can seed the game's config files there).
- Native extensions: the ANE's `library.swf` code loaded before the app (with the AIR API version set
  while loading, or names like `flash.external.ExtensionContext` do not resolve), and
  `ExtensionContext.call` answered by built-in replies (a joystick ANE answers "no joystick").
- `NativeApplication.exit()` mapped to quit.

### ruffle_sdl: no Westonpack

Desktop Ruffle needs X11 or Wayland, hence Westonpack. The fork adds an SDL2 front end instead: SDL
window and GLES 3.0 context, Ruffle's wgpu renderer on that context, the movie drawn into one
texture and blitted (Y flipped) to the screen, SDL keyboard, mouse and audio. It needs only
`libSDL2` and `libasound` (glibc 2.30, 16.8 MB), and gptokeyb2 maps the pad to keys. FlashPunk games
set the stage to top-left alignment, so the front end forces centred show-all scaling on every
screen size. Cross-built with host clang/lld against a bullseye sysroot
([building native code](../building-native-code.md)).

### Performance lessons

These are about Ruffle on the Mali-G31 / A53 class, but the method applies to any player or VM:

- **Measure the game's clock, not the player's frames/s.** FlashPunk runs a fixed-step loop inside
  one Timer callback (several updates and one render per event); Ruffle's frame counter and logic
  timer never see it. The game's own timer and a per-method script profile do.
- **Catch-up timers multiply work when slow**: Ruffle fired a late repeating Timer up to 10x per
  update and each event rendered again. Firing a late timer once and continuing from now took FULL
  graphics from 4 to 32 updates/s on the PC.
- **Count GPU->CPU readbacks**: each costs ~10 ms on the Mali-G31 blob whatever its size
  ([Mali driver lessons](../graphics.md#mali-driver-lessons)). Remove what forces them (text drawn
  into bitmaps, CPU and GPU blits mixed into one buffer). The port draws text and transformed
  bitmaps on the CPU when the target is on the CPU, and keeps the game's screen buffer as a GPU
  command list.
- Per-operation thread pools lose: waking the pool costs more than a 640x480 blend.
- **Patch the game when the game wastes the time.** Offspring Fling rebuilt every image and text
  buffer on every 60 Hz update although it draws once per Timer event (70 % of an update). Its AS3
  is obfuscated and cannot be recompiled, but single method bodies can be replaced as P-code with
  JPEXS (`ffdec-cli`), shipped as a small xdelta applied by the patcher on first launch. Keep the
  SWF's file name (Ruffle keys saves on it) and the original as `.orig`; run unknown versions
  unpatched.
- **Profile loading before guessing.** The suspected double XML parse was a decompiler artefact
  (~10 % of init); the real costs were decoding 120 bundled replays just to count their frames and a
  getPixel/setPixel recolour loop over 3.5 Mpx. A precomputed table and one `BitmapData.threshold`
  call (exact for binary-alpha images) halved init and level loads.
- One full garbage collection plus `malloc_trim` right after any script tick longer than 1 s
  returns the loading peak at once; collections between ticks did nothing and cost 0.4-0.6 s each
  on an A53.

Offspring Fling on the RG Cube XX (Knulli, Mali-G31, 2026-10-06), same tutorial run:

| Stage | Game updates/s (of 60) | Screen updates/s | Notes |
|--|--|--|--|
| Morning (fork with AIR APIs, OOM fixed) | 27 | 5.3 | 550-660 MB RSS, 27 % of the time in readbacks |
| Game patches v2 + CPU text path | 60 | 18.5-20.4 | init 3.1 s, level start 1.6 s, 480 MB RSS |
| GPU-resident screen buffer | 60 | 28-29 | vsync half rate; loads under 1 s |
| Game patch v3 (tint at draw time), buffer on by default | 60 | 54-59 | render 4.7 ms, ~500 MB RSS |

On a TrimUI Smart Pro (Knulli, PowerVR, 1280x720) the same game went from 31 to 60 game updates/s and
from 6.2 to 16-18 screen updates/s with the game patch and the CPU text path.

### Automating first-run tests

PortMaster's patcher dialog waits for a pad button that a virtual keyboard cannot press, so an
automated first run hangs there. For unattended device runs, apply the patch by running the
patchscript directly (with `GAMEDIR` and `controlfolder` exported) before starting the game. More
in [testing and debugging](../testing-and-debugging.md).
