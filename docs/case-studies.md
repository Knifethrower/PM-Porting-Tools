# Case studies

One paragraph per port: what kind of game it is, the route taken, and what it taught. The
lessons themselves live on the topic pages linked from each entry. Most of these ports are work in
progress and not (yet) in PortMaster; the status lines are as of early October 2026 and name the
device they were measured on.

Devices: RG353V (RK3566, Mali-G52, dArkOS), RG351P (RK3326, Mali-G31, ROCKNIX), RG Cube XX (H700,
Mali-G31, Knulli, 720x720), TrimUI Brick (A133P, PowerVR, muOS). See
[devices and firmwares](devices-and-firmwares.md).

## Unity, Linux builds under box64

**Night in the Woods** (Unity 5.6, Mono, FMOD Studio). The first port and the origin of the
conversion pipeline: GLCore shaders rewritten to GLES 3, DXT textures re-encoded to ASTC on the
device, FMOD's sample rate matched to the firmware's mixer, and the glespass shim so the GLES game
runs on Westonpack's crusty without gl4es. Threaded rendering through a real context handover took
play from a median of 11 to 16.5 fps on the RG353V. Profiling under box64 showed the main thread,
not the GPU, is the limit. Black frames on ROCKNIX with libmali stayed unsolved.
[Unity](engines/unity.md), [graphics](graphics.md), [performance](performance-and-memory.md).

**Gone Home** (Unity 2018.4, 3D, deferred + HDR). The same pipeline carried over almost unchanged
and plays very well on the RG353V. New lessons: game time must not depend on the frame rate (check
it in every port that runs below its target fps), PCM audio converted to IMA ADPCM, and game
options files that override the command line. Its installer and Night in the Woods' were merged
into one tool, [unityport](../unity/unityport/). [Unity](engines/unity.md).

**Firewatch** was started from Gone Home's layout (tools copied); no port yet.

## Unity 4

**Ittle Dew** (GOG Linux, Unity 4.7.1, 2D). The simplest case at first: nothing had to be converted
(setup now re-encodes textures to ASTC and patches the player), and the player runs under box64
with gl4es. It still produced most of the Unity 4 knowledge: gl4es bugs (ARB program id
recycling, PC no-test mode lying about render textures), the resolution dialog plugin, a lean
launcher, debugging on the device over SSH, and finally a build without Westonpack (an X11
stand-in plus GLX on the firmware's SDL2) that saved memory on 1 GB devices. Works on the RG353V,
RG351P, TrimUI Brick and RG Cube XX (60 fps). [Unity 4](engines/unity-4.md),
[graphics](graphics.md), [testing](testing-and-debugging.md).

**Teslagrad** (Steam Linux, Unity 4.7.2). The Ittle Dew runtime, unchanged. New: running a Steam
Linux build without the client (`steam_appid.txt`), moving Steam-Cloud-only saves to local files,
a gl4es bug that read 16-bit alpha textures as bytes and hid every point light, and a 16:9
letterbox through gl4es's main framebuffer so every screen shape works without patching game
data. Full first-run setup from untouched Steam files took under 5 minutes (283 s) on the RG Cube XX.
[Unity 4](engines/unity-4.md).

**Usagi Yojimbo: Way of the Ronin** (Windows only, Unity 4.5). The first game without a Linux
build: its Windows data runs on a Unity 4.5 Linux player (version string patched for its 4.5.3
data), with setup that converts the user's own files on the device (verified on the PC and under
qemu; the personal RG353V build ran pre-converted data). Taught per-platform serialization
differences, native plugin stubs, patching managed code in pure Python and mouse-only menus.
[Unity 4](engines/unity-4.md), [donor ports](engines/unity-4-donor.md).

**Ascendant** and **Thomas Was Alone** (Unity 4.5). Ascendant is a plain GOG Linux port on the
Ittle Dew runtime (its Steam Linux build is 32-bit only); Thomas Was Alone has no Linux 64-bit
build and runs as a donor port, its data converted to Unity 4.7.2's layouts. Ascendant is
PC-tested; Thomas Was Alone also runs on the RG Cube XX (setup 21 s, Oct 2026).
[Unity 4](engines/unity-4.md), [donor ports](engines/unity-4-donor.md).

## Native: source ports and reimplementations

**Hocus Pocus** (a native SDL2 reimplementation). Packaging lessons for native ports: what the
launcher needs and what PortMaster expects in the release folder. [Native](engines/native.md),
[packaging](packaging.md).

**Torus Trooper** and **Mu-cade** (D, OpenGL 1.x). Rewritten to draw through native GLES 2.
D's `real` turned out to be software floating point on aarch64 and cost most of the frame time
until replaced. Torus Trooper runs at 56-61 fps on the RG Cube XX. Their old OGG music played
muffled through stb_vorbis until losslessly repacked ([audio](../audio/)). Mu-cade fills every
screen shape. [Native](engines/native.md), [performance](performance-and-memory.md).

**Cube** (C++, OpenGL 1.x). 8-22 fps through gl4es, 60 fps with a small batching GLES 2 layer on
the RG Cube XX: the measurement behind "native GLES, not gl4es, when you have the source".
[Graphics](graphics.md#native-gles-2-when-you-have-the-source), [GLES layers](../gles/).

**Bugdom 2** (C/C++, OpenGL 1.x). An own OpenGL 1.x on GLES 2 layer with lighting, fog, texgen and
two texture units; matches desktop GL on all ten levels on the PC. The third game where keeping the
GL 1.x calls and implementing them on GLES 2 worked. [GLES layers](../gles/).

**OpenLoco**, **3D Movie Maker**, **Stunt Playground**, **Open Surge**, and an update of the
existing **KeeperFX** port. Larger native ports: OpenLoco needs SDL3 and runs on an SDL3-on-SDL2
shim; 3D Movie Maker bundles FluidSynth for its MIDI music; Stunt Playground is a rewrite of its old middleware stack (Ogre, Newton,
CEGUI) on current Ogre and Bullet; Open Surge needed an Allegro 5 fix ([allegro](../allegro/)).
[Native](engines/native.md), [building](building-native-code.md).

**A batch of small native ports** (Taisei, TBFTSS, The Legend of Edgar, Domino-Chain, HyperRogue,
Tatham's Puzzles, Planet Blupi, Toppler) built and device-tested in one night on the RG Cube XX
with scripted input. Lessons about checking PortMaster for an existing port first (Toppler already
existed as "Nebulus"), upstream policies, and what breaks across many unrelated code bases.
[Native](engines/native.md), [choosing an approach](choosing-an-approach.md).

## LÖVE

About twenty LÖVE games (Techmino, Sienna, Zabuyaki, Duck Marines, Orthorobot, TROSH, Lonoma, Star
Phase, A Village in the Sky and others) on PortMaster's love_11.5 runtime. Lessons: running 0.8,
0.9 and 0.10 games on LÖVE 11 with a compat shim, filling the screen with one `require`
([fit.lua](../love/)), keyboard loss after an MSAA window rebuild on KMSDRM, and memory on 1 GB
devices. [LÖVE](engines/love.md).

## Godot

**Dome Keeper** (Godot 4, Steam build). A shipped pack running on a stock Godot 4.3 template, with
the Steam and PlayFab singletons stubbed: reaches the title screen on the PC.
**Cassette Beasts** (Godot 3.5, custom engine build): the stock engine runs the unmodified pack,
but memory (~805 MB at the title, ~895 MB entering a new game, on the device with temporary zram)
and GLES3 shaders were the problems. A custom frt build now reaches the Overworld in 1 GB (~29 fps,
capped at 30); not finished.
**Eight Godot 3 source projects** (A Key(s) Path, Jetpaca, Holonomy, Wind-Up Racer and others) exported
for PortMaster's frt_3.5.2 runtime: GLES2 with ETC1 textures, a software pointer, and shadow cost on
the Mali-G31 (31 vs 60 fps). [Godot](engines/godot.md).

## GameMaker

**Daydreamer: Awakened Edition** (GameMaker 1.4). Too big for 1 GB devices because of images loaded
from disk at runtime. ASTC packs made on the device and a patched gmloader-next took it from dying
at the first cutscene to 410 MB peak on the RG Cube XX. [GameMaker](engines/gamemaker.md).

## Windows, Flash and macOS

**Box64 + Wine runtime** (Peggle Deluxe, test programs). Wine under box64 on Westonpack, with
xbridge to get Wine's X11/GDI windows on the screen. 2D programs (GDI, DirectDraw) run on the
RG353V (dArkOS); xbridge's picture is verified on the PC, a device confirmation is still pending.
The first game, Peggle Deluxe, needs wined3d's OpenGL even in 2D mode and is blocked on OpenGL on
the device.
[Wine and Flash](engines/windows-wine-and-flash.md).

**Offspring Fling** and **Fancy Pants Adventures** (Adobe AIR / Flash). Run on Ruffle instead of Wine:
Offspring Fling on an own Ruffle fork with AIR APIs and an SDL2 front end (60 updates/s on the RG
Cube XX), Fancy Pants on crxssrazr93's public port. [Wine and Flash](engines/windows-wine-and-flash.md).

**Spaghetti Celesti** and **Tummy Bonbons** (macOS arm64 builds through machismo). Start from an
existing port of the same engine and scan for ARMv8.1+ instructions a Cortex-A53 cannot run. LuaJIT
went two ways: Spaghetti Celesti keeps the game's own LuaJIT (its Lua uses the developer's parser
additions; machismo emulates the missing instructions), while Tummy Bonbons' built-in LuaJIT
panicked, so a trampoline replaces it with a Linux build. Both play on the RG Cube XX.
[macOS](engines/macos-machismo.md).

## Apps

**Jellyfin MPV Shim** (Python on libmpv). mpv drawing through the firmware's SDL2 instead of DRM,
cross-built against an old glibc; plays video on the RG Cube XX. [Apps](engines/apps-and-video.md).
