# Choosing an approach

Which route to take for a given game, what each route costs, and why some games were not ported at
all. This page is an overview: each row and section links to the page that has the details and the
measurements. Terms like box64, gl4es and Westonpack are explained in the [glossary](README.md#glossary).

## First checks (minutes, before any work)

- **Is it already in PortMaster?** Search the catalogue (`ports.json` from PortMaster-Info, the
  PortMaster-New `ports/` tree and its open PRs) by the upstream name and description too, not only
  the title. Toppler was already there as "Nebulus"; titles with dots ("mr.boom") hide from a plain
  search.
- **Licence and rights.** Code licence, but also art, music, fonts and sound effects. Many open
  source games have assets with no licence text, "personal use only" terms, or another company's
  characters, and PortMaster will not take those. In the author's batches several finished ports
  were held back because assets (a font, a tileset, sound clips) had no stated licence or used
  other companies' characters. Check before the work, not after. What the port itself must ship:
  [packaging](packaging.md#licences).
- **Upstream policy.** Read the upstream README. Some projects state that AI-generated code is not
  accepted; respect that before spending time.
- **Can it be played with a pad?** Mouse-driven UIs, phone-style text entry, portrait screens and
  online-only multiplayer each cost the author candidates.
- **Which builds exist, and from which store?** Builds differ between stores (engine version,
  32-bit only, DRM). Prefer the DRM-free store's build, and support one store if that is all that
  works: say so in the README and have the setup detect the others. Unity examples:
  [Unity 4](engines/unity-4.md#is-the-game-a-candidate).
- **RAM.** Many devices have 1 GB. A game whose desktop RSS is near 1 GB needs work or a `2gb`
  requirement (see [devices-and-firmwares](devices-and-firmwares.md#ram-classes)).
- **Nothing from the game is shipped.** The user copies their own files in; anything that must
  change is converted or patched on the device on first launch
  ([packaging](packaging.md#first-run-setup-with-the-patcher)).

## Decision table

| What you have | Route | Status in the author's ports (as of Oct 2026) | Details |
|--|--|--|--|
| Source code (open source, or a reimplementation) | Native aarch64 build against the firmware's SDL2; replace desktop GL with a native GLES 2 path | Many ports, 60 fps typical on an H700 | [engines/native](engines/native.md), [building-native-code](building-native-code.md) |
| Unity 5.x to 2017.3 with a Linux x86_64 build | box64 + the game's Linux player; on-device conversion of shaders (GLCore -> GLES3) and textures (DXT -> ASTC); Westonpack (crusty) for GLX | Works: 10-30 fps on RK3566 after tuning | [engines/unity](engines/unity.md), [unityport](../unity/unityport/) |
| Unity 4 with a Linux x86_64 build | box64 + the player + gl4es, X11 and GLX provided in-process on the firmware's SDL2 (no Westonpack) | Works: 50-60 fps on the RG Cube XX | [engines/unity-4](engines/unity-4.md), [xstub/glxsdl](../shim/xstub/) |
| Unity 4 with only a Windows, Mac or 32-bit Linux build (Mono) | Convert the game's data to run on one Unity 4.7.2 Linux x86_64 player (the donor technique), then as above | Works on PC for 4.0-4.7 data; first device runs (RG353V, RG Cube XX) Oct 2026, unreleased | [engines/unity-4-donor](engines/unity-4-donor.md) |
| Unity 5.x to 2017.3 with only a Windows build | Player swap only if the build carries GLCore shaders, which Windows 5+ builds rarely do; IL2CPP builds would need Wine | Not done | [engines/unity](engines/unity.md) |
| LÖVE game | PortMaster's `love_11.5` runtime (in the control folder, nothing downloaded or compiled); LÖVE 0.8/0.9/0.10 games through a compat shim | Many ports, 60 fps typical | [engines/love](engines/love.md), [love tools](../love/) |
| Godot 3 project or pack | PortMaster's `frt_3.5.2` runtime; source projects exported to a `.pck` (GLES2, ETC1 textures) | Several 2D ports; 3D works with shadows off | [engines/godot](engines/godot.md), [godot tools](../godot/) |
| Godot 4 pack (Steam or itch build) | The pack on a stock Godot template of the same version, with stubs for Steam/PlayFab | PC only: reaches the title, RAM is the problem | [engines/godot](engines/godot.md) |
| Godot 2 | PortMaster's `frt_2.1.6` runtime | Did not run a one-line script on the RG Cube XX (Knulli); skipped | [engines/godot](engines/godot.md) |
| GameMaker (`data.win`) | gmloader-next with the Android runner, gmtoolkit for audio and textures on first launch | Works: 60 fps on the RG Cube XX (one WIP port, Oct 2026) | [engines/gamemaker](engines/gamemaker.md), [gamemaker tools](../gamemaker/) |
| Adobe AIR / Flash SWF | Ruffle with an SDL2 + GLES 3 front end (no Westonpack) | Works after Ruffle and game-side fixes (one WIP port, Oct 2026) | [engines/windows-wine-and-flash](engines/windows-wine-and-flash.md) |
| macOS arm64 build (no Linux build) | machismo: runs the Mach-O natively on Linux aarch64, dylibs mapped to Linux libraries | Two ports playing on the RG Cube XX | [engines/macos-machismo](engines/macos-machismo.md), [machismo tools](../machismo/) |
| Java / libGDX | PortMaster's Zulu JRE runtime (Azul's free build of the Java runtime, Java 17) plus Westonpack (`weston_pkg_0.2`) for the window | One port (Space Trader): current version passed on dArkOS; muOS, AmberELEC and ROCKNIX passed the previous version; Knulli untested | |
| FNA / XNA (.NET) | FNA has a native GLES path (`FNA3D_OPENGL_FORCE_ES3=1`: no gl4es, no Westonpack); older FNA/XNA assemblies run on PortMaster's Mono runtime. MonoGame's desktop build always asks for desktop GL and needs gl4es | Not done by the author | |
| Windows-only, no engine route above | Box64 + Wine runtime | Runtime built, Wine runs on the RG353V (dArkOS); the X11/GDI output bridge is not yet confirmed on a device; the first game (Peggle Deluxe) needs wined3d's OpenGL even in 2D mode and is blocked on device OpenGL | [engines/windows-wine-and-flash](engines/windows-wine-and-flash.md), [wine tools](../wine/) |
| App (Python, video) | PortMaster's `python_3.11` runtime; libmpv or LÖVE for the UI | Two apps | [engines/apps-and-video](engines/apps-and-video.md) |
| Only an Android build | Not covered by these pages, see [below](#why-not-the-android-player) | | |

Order of preference when more than one row fits: source port, then a native runtime (LÖVE, Godot,
GameMaker, machismo, Ruffle), then box64 with the game's Linux build, then a player swap or donor,
and Wine last. A native Linux build through box64 is always better than the Windows build through
Wine.

## Games with source: native GLES, not gl4es

When the renderer can be changed, replace its OpenGL 1.x/2.x calls with a native OpenGL ES 2.0 path
instead of shipping gl4es. gl4es is for binaries that cannot be changed (Unity players, closed
engines). gl4es turns each immediate-mode block into its own draw and the Mali blob pays per draw:
Cube went from 8-22 fps through gl4es to 60 fps with its own GLES 2 layer on the RG Cube XX. The
pattern that worked three times (Torus Trooper, Cube, Bugdom 2) keeps the game's GL 1.x calls and
implements them in one file on GLES 2; two such layers are in [gles](../gles/). Measurements and
technique: [graphics](graphics.md#native-gles-2-when-you-have-the-source).

Measure on the device before rewriting a renderer: Torus Trooper's real bottleneck was D's `real`
type in software arithmetic, not draw calls ([engines/native](engines/native.md)).

Games that need desktop OpenGL 3.3 core (Naev) cannot run on a Mali-G31 at all. Large desktop-GL
codebases without a GLES path (AstroMenace, Blob and Conquer) need a renderer port first, and SDL 1.2
games need an SDL2 port first; both were judged too big for a quick port.

## Closed binaries: what the box64 routes cost

- **CPU.** Under box64 a Unity game is CPU-bound on its main thread (Mono scripts plus engine); the
  GPU and render thread have headroom. Expect 10-30 fps on RK3566 after tuning
  ([performance-and-memory](performance-and-memory.md)).
- **Memory.** gl4es decodes DXT textures into RAM, and Mali shares system memory; box64 itself
  costs little. The measured split is on
  [performance-and-memory](performance-and-memory.md#measure-where-the-memory-goes).
- **Loading time.** box64 plus Mono's JIT makes loads slow; Unity's own loading knobs did not help.

Details: [box64](box64.md), [graphics](graphics.md#gl4es).

For Unity, the build, scripting backend (Mono or IL2CPP), version, shaders, plugins and RAM decide
the route; check them with [Unity 5.x to 2017.3](engines/unity.md#is-the-game-a-candidate) or
[Unity 4](engines/unity-4.md#is-the-game-a-candidate). Only Mono builds can be moved to another
player.

## Routes tried and rejected

For the first Unity port (Night in the Woods, Unity 5.6), these were tried or estimated before box64
won:

| Route | Verdict |
|--|--|
| Replace the engine, re-render assets from the game data | Renders looked bad, far too much work |
| Reuse a console (Vita) port, Vita3K, a native loader | Dead end for Linux ARM |
| The Android build's ARM `libunity.so` through an Android loader | Estimated, not tried: needs a Bionic -> glibc bridge and a Java/JNI layer (below) |
| Native ARM64 Mono under box64 (bridging ~3,600 internal calls) | Weeks of work with GC, exception and signal problems. Profiling showed C# is only ~24% of the main thread, so the gain would be small |

The general lesson: profile before a big rework. The sampling method is in
[performance-and-memory](performance-and-memory.md#under-box64).

## Why not the Android player?

An Android build looks tempting: a native ARM engine (no box64 cost), GLES shaders and ETC textures
already. The author did not take this route for the Unity versions these pages cover (4.x to 2017.3);
this section records why, not a verdict on Android builds in general.

The player swap that works from Windows to Linux does not carry over directly. The Linux player is a
self-contained program (X11 window, OpenGL, its own input, data in plain files), and Windows and Linux
builds share one data format. Android's `libunity.so` (with `libmono.so`, `libmain.so`) is a library
made to run inside Android:

| It expects | On a Linux handheld |
|--|--|
| Bionic libc and the NDK linker | glibc: every libc, pthread and linker symbol needs a bridge |
| A Java activity that starts the engine, feeds input and lifecycle, and answers hundreds of JNI calls (system info, PlayerPrefs, screen, locale) | No JVM: every Java method used needs a fake |
| An `ANativeWindow` from Java | An EGL window (SDL) instead |
| Data read from the APK via `AAssetManager` | A replacement file layer |
| OpenSL ES audio, `liblog`, system properties | More shims |

Bridging that is an Android loader's job (so_loader style, like gmloader for GameMaker).
GameMaker's runner needs a small Java surface; Unity's is larger, and each game's plugins (ads,
billing, analytics) add more. Other points the author ran into:

- The Android players of these Unity versions are 32-bit ARMv7: they need an armhf userland, which
  not every firmware ships.
- Android data (GLES2 shaders, ETC/PVRTC textures, MP3/Vorbis audio) is readable only by the
  Android player; the halves cannot be mixed.
- IL2CPP Android builds bake the scripts into `libil2cpp.so`: no managed DLLs to patch.
- Waydroid or another Android container is too heavy for 1-2 GB handhelds.

For the author's ports, box64 with the game's desktop player was the quicker route. Check PortMaster
for existing tools before starting on an Android build.

## Why games were skipped

From the author's candidate lists (Oct 2026). Check these first.

| Reason | Examples |
|--|--|
| Already in PortMaster | Many: found only after a search by upstream name |
| Assets not redistributable or without a licence | Art and music "for personal use only"; game-jam games with no licence at all; asset packs from stores with no redistribution terms |
| Another company's characters or names | Fan remakes of Nintendo, Namco and Tetris games; a remake of an Ocean game |
| Online-only or online-core | Online multiplayer games, games built around an online leaderboard |
| Controls | No controller support with a mouse UI; controls not handheld-friendly |
| Engine runtime missing or broken | Godot 2 (frt_2.1.6 did not run); Godot 4 (no Godot 4 runtime route on the devices yet; Godot 4 3D games too heavy for the Mali-G31) |
| Graphics API | OpenGL 3.3 core (not on Mali-G31); large desktop-GL codebases |
| Too old an SDL | SDL 1.2 games (would need an SDL2 port first) |
| Too big | Very large codebases, heavy dependencies (Guile, legacy desktop GL) |
| Needs commercial data the user is unlikely to have, or a portrait screen | |

Unclear asset licences do not always mean "skip": the port can still work, but it should not be
submitted until the rights are clear.

## After choosing

- Build the smallest port that runs, and compare the launcher with recently merged ports of the
  same type: AI-built ports whose launcher is substantially longer or more complicated than merged
  ones are rejected without discussion, and any logic merged ports lack needs a game-specific reason
  ([packaging](packaging.md#what-reviewers-reject)).
- Test on the PC first, then on as many firmwares as possible
  ([testing-and-debugging](testing-and-debugging.md),
  [devices-and-firmwares](devices-and-firmwares.md)).
- One paragraph per finished port, with the route it took: [case-studies](case-studies.md).
