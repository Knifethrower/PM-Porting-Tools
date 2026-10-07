# Porting games to PortMaster: notes from the field

What was learned working on about sixty games and apps (many still experimental) for [PortMaster](https://portmaster.games) on
aarch64 Linux handhelds (RK3566, RK3326, H700 and A133P devices on dArkOS, ROCKNIX, Knulli, muOS and
AmberELEC), from Unity games under box64 to native source ports, LÖVE, Godot, GameMaker, Wine and
Flash. The tools mentioned here are in this repository; each page links to them.

These are working notes turned into a guide, not official PortMaster documentation. The ports,
the tools in this repository and these notes were made with AI assistance (Claude), and tested on
real devices by the author. Most of the
ports behind them are unreleased or experimental. Numbers are measurements on a named device and
date (mostly September and October 2026); firmwares and runtimes change, so check anything marked
"as of" before relying on it. PortMaster's own rules always come first: see
[packaging](packaging.md).

## Start here

1. [Choosing an approach](choosing-an-approach.md): which route fits which kind of game, what each
   costs, and why games were skipped.
2. [Devices and firmwares](devices-and-firmwares.md): SoCs, GPUs and drivers, RAM classes, screen
   sizes, and how each firmware differs.
3. The engine page for your game (below), and the [case studies](case-studies.md) for ports of the
   same kind.
4. [Packaging](packaging.md) before you write the launcher, and again before the pull request.

## Topics

| Page | What |
|--|--|
| [Packaging](packaging.md) | What reviewers expect and reject, lean launchers, port.json, the patcher and first-run conversion on the device, Python on the device, licences, line endings. |
| [Building native code](building-native-code.md) | Targeting an old glibc, the Debian bullseye arm64 chroot, cross toolchains, no static core libraries, ARMv8.0 portability, checking a binary. |
| [Graphics](graphics.md) | Native GLES 2 vs gl4es, gl4es's costs and bugs, Westonpack and crusty, GLX on the firmware's SDL2, filling every screen shape, Mali driver lessons. |
| [box64](box64.md) | Building it, settings that matter (and ones that break Mono), libraries, signals, memory and CPU cost. |
| [Performance and memory](performance-and-memory.md) | Measuring fps, profiling native code and code under box64, threads and cores, per-frame GPU costs, 1 GB devices. |
| [Testing and debugging](testing-and-debugging.md) | Testing without a device (qemu-user, Xvfb, a fake PortMaster), testing on your own device over SSH, logs to ask testers for, hangs and crashes. |

## Engines and kinds of game

| Page | What |
|--|--|
| [Unity 5.x to 2017.3](engines/unity.md) | Linux builds under box64: GLES 3 shaders, ASTC textures, FMOD, audio, game time vs frame rate. |
| [Unity 4](engines/unity-4.md) | Unity 4 under box64 and gl4es without Westonpack, 16:9 on every screen, Steam builds, patching managed code. |
| [Unity 4 donor ports](engines/unity-4-donor.md) | Windows, Mac and 32-bit Linux Unity 4 data on one 4.7.2 Linux player you build yourself; converting between 4.x layouts. |
| [Native ports](engines/native.md) | Source ports and reimplementations: SDL2, own GLES 2 renderers, D, SDL 1.2 code, input, audio, saves. |
| [LÖVE](engines/love.md) | PortMaster's love_11.5, older LÖVE versions, filling the screen, input, memory. |
| [Godot](engines/godot.md) | Godot 3 projects on frt, shipped Godot 4 packs on stock templates, memory. |
| [GameMaker](engines/gamemaker.md) | gmloader and gmloader-next, gmtoolkit, images loaded at runtime. |
| [Windows, Wine and Flash](engines/windows-wine-and-flash.md) | A Box64 + Wine runtime and its limits; Flash and Adobe AIR games through Ruffle. |
| [macOS through machismo](engines/macos-machismo.md) | Mac arm64 builds on Linux, instructions a Cortex-A53 cannot run, LuaJIT. |
| [Apps and video](engines/apps-and-video.md) | Python apps, libmpv on SDL2, an app written for LÖVE. |
| [Case studies](case-studies.md) | One paragraph per port: route taken and what it taught. |

## Glossary

PortMaster and firmwares:

| Term | Meaning |
|--|--|
| PortMaster | The port manager and launcher framework used by most handheld Linux firmwares. Ports are a launcher script plus a folder, installed into the firmware's `ports` directory. |
| CFW, firmware | The handheld's Linux distribution: dArkOS / ArkOS, ROCKNIX, Knulli, muOS, AmberELEC. |
| Front end, EmulationStation | The firmware's menu that lists games and ports and starts them. Most firmwares use EmulationStation or a fork of it; muOS has its own. |
| PortMaster-New | The [repository](https://github.com/PortsMaster/PortMaster-New) ports are submitted to by pull request. |
| ports.json | The catalogue of all published ports (in the PortsMaster/PortMaster-Info repository), with fields such as `date_added`. |
| harbourmaster | PortMaster's command-line back end (in PortMaster-GUI): installs ports, fixes their permissions and downloads runtimes. |
| Control folder, control.txt | PortMaster's own folder on the device (`$controlfolder`). Every launcher sources its `control.txt`, which sets `$CFW_NAME`, `$DEVICE_ARCH`, `$ESUDO`, `$GPTOKEYB2` and the `pm_*` helpers. |
| ESUDO | Variable from `control.txt`: `sudo` on firmwares that run ports as a normal user (dArkOS / ArkOS), empty where ports run as root. |
| Runtime | A squashfs image PortMaster downloads and the launcher mounts, listed by key in `port.json`'s `runtime`: `weston_pkg_0.2` (Westonpack), `mesa_pkg_0.1`, `python_3.11`, `frt_3.5.2`. |
| love_11.5 | PortMaster's LÖVE 11.5. Unlike the runtimes above it ships in the control folder (`runtimes/love_11.5`): nothing is downloaded, and LÖVE ports have `"runtime": []`. |
| gptokeyb / gptokeyb2 | PortMaster's tool that turns gamepad input into keyboard and mouse events for games without pad support. |
| Patcher | PortMaster's on-screen first-run setup: runs a port's script that converts the user's game files on the device. |
| patchscript | That script (`tools/patchscript` in the author's ports): unpacks, converts and patches the user's files, then writes a flag file. |

Running other programs:

| Term | Meaning |
|--|--|
| box64 | Runs x86_64 Linux programs on aarch64 with a dynamic recompiler. |
| dynarec | box64's dynamic recompiler: translates x86_64 code to ARM code while the program runs. |
| DynaCache | box64 0.4.x's on-disk cache of translated code (`$HOME/.cache/box64`), on by default. |
| box86, box32, WoW64 | box86 runs 32-bit x86 Linux programs and needs a 32-bit (armhf) userland; box32 is box64's own 32-bit mode; WoW64 is Wine's mode that runs 32-bit Windows programs inside a 64-bit Wine, so box64 alone is enough. |
| Mono, IL2CPP | Unity's two scripting backends: managed code with a JIT, or C# compiled to native code. |
| Donor player | A Unity Linux player of the right version used to run a game's Windows or Mac data. |
| unityport | The converter in this repository that turns a Unity 5.x to 2017.3 game's shaders, textures and audio into what the devices can use, on the device. |
| frt | A Godot 3 platform port for small Linux devices; PortMaster's frt runtimes are built on it. `frt_3.5.2` is FRT 2.1.0 on Godot 3.5.2, with an SDL2 backend. |
| gmloader, gmloader-next | Loaders that run a GameMaker game's Android runner on Linux. |
| gmtoolkit | The tool current PortMaster GameMaker ports use to compress a game's audio and textures on first launch. |
| machismo | A loader that runs macOS arm64 programs on Linux. |
| Ruffle | A Flash Player emulator written in Rust; runs Flash and (with additions) Adobe AIR games. |

Graphics:

| Term | Meaning |
|--|--|
| gl4es | Translates desktop OpenGL 1.x/2.x calls to OpenGL ES 2. |
| Westonpack | PortMaster's runtime with a headless Weston compositor, Xwayland and crusty, for X11/GLX programs. |
| crusty | Westonpack's GLX implementation on EGL/SDL, usually paired with gl4es. |
| glespass | A shim from these ports: lets a game that already renders GLES run on crusty without gl4es. |
| xstub, glxsdl | Stand-ins from these ports: an X11 library and GLX on the firmware's own SDL2, so Unity 4 players run without Westonpack. |
| GLES layers | Small OpenGL 1.x-on-GLES 2 implementations for games with source ([gles](../gles/)). |
| Mali blob, libmali | ARM's closed GLES driver (most RK3566/RK3326 firmwares). Panfrost is the open Mesa driver some ROCKNIX builds use instead. |
| Mesa, llvmpipe | Mesa is the open source OpenGL / GLES implementation; llvmpipe is its CPU renderer, used to test GLES code on a PC. |
| KMSDRM, fbdev | Ways SDL2 draws directly to the screen without a window system. |
| FBO, VBO | Framebuffer object (an off-screen render target) and vertex buffer object (vertex data held by the GPU driver). |
| GrabPass | A Unity shader pass that copies what is drawn so far into a texture; expensive through gl4es on GLES. |
| DXT (S3TC), ASTC, ETC | Compressed texture formats. Desktop games ship DXT; Mali GPUs have ASTC and ETC but no DXT. |

CPU, memory and testing:

| Term | Meaning |
|--|--|
| LSE | Large System Extensions: ARMv8.1 atomic instructions. The Cortex-A53 and A35 in most handhelds are ARMv8.0 and lack them. |
| SIGILL | The signal a program gets for an illegal instruction, usually one the CPU does not have (for example LSE on a Cortex-A53). |
| RSS | Resident set size: the memory a process actually holds in RAM. |
| qemu-user | Runs aarch64 Linux binaries on an x86_64 PC, used for testing without a device. |
| Xvfb | A virtual X server; lets games run and be screenshotted on a PC with no display. |
