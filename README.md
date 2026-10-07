# PM-Porting-Tools

Tools written while porting games to [PortMaster](https://portmaster.games) on aarch64 Linux
handhelds (RK3566 / RK3326 / H700 devices on dArkOS, Knulli, ROCKNIX, muOS). They come from these
mostly unreleased and experimental WIP ports: Night in the Woods, Gone Home, Usagi Yojimbo, Ittle Dew (Unity via box64), Torus Trooper,
Mu-cade, Cube, Hocus Pocus, 3D Movie Maker, OpenLoco, Stunt Playground, Bugdom 2, Open Surge
(native), Dome Keeper (Godot 4), Spaghetti Celesti and Tummy Bonbons (macOS arm64 via machismo),
a batch of LÖVE games and a Box64 + Wine runtime. Only original code is here: no game files, no binaries, no third-party sources. Patches to
other projects are included as patch files.

Most scripts assume a Windows host with WSL (Ubuntu 24.04), Git Bash and Python 3; paths in the
examples are the ones the ports used, so adapt them.

| Folder | What |
|--|--|
| [`packaging/`](packaging) | `pm_zip.py`: build a port zip from a release folder (exec bits from ELF/shebang/.so, LF endings). `pm_lint.py`: check a port against PortMaster's packaging rules. `repack_launcher.py`. |
| [`devtools/native-testing/`](devtools/native-testing) | Fake PortMaster device for a port zip (qemu-user + Xvfb); deterministic frame dumps and pixel diffs (x86_64 and aarch64); aspect-ratio contact sheets. |
| [`devtools/device/`](devtools/device) | On the handheld through a shared SSH connection: run a port with the front end stopped, scripted uinput keys, framebuffer screenshots, thread and memory sampling. |
| [`devtools/profiling/`](devtools/profiling) | Self-starting SIGPROF sampler and report, GL call counter / frame dumper / scripted input preload (`glcount.c`), fps log preload for box64 games, Mono/box64 profiling tools. |
| [`devtools/pc-testing/`](devtools/pc-testing) | Run Unity Linux builds on the PC as GLES 3 (Mesa), drive them in WSLg/Xvfb, gl4es patches that mimic the Mali driver, a fake PortMaster patcher for first-run setup scripts. |
| [`build/native-aarch64/`](build/native-aarch64) | Debian bullseye arm64 chroot runner and a clang cross toolchain file: binaries that need only glibc 2.31. |
| [`audio/`](audio) | Lossless Vorbis residue type 0 to type 1 repacker (old OGGs play muffled through stb_vorbis in SDL_mixer 2.8), its verifier, an SDL_mixer output dumper. |
| [`wine/`](wine) | xbridge (shows Wine's X11/GDI windows on the screen through SDL2 under Westonpack, with gptokeyb input), GLX check, GDI/DirectDraw/D3D9 test programs, a box64 patch for fixed-base 32-bit programs, prefix tools. |
| [`shim/`](shim) | glespass (libGL.so.1 for Westonpack's crusty_glx without gl4es, for GLES-converted Unity games) with its tests; xstub + glxsdl (Unity 4 without Westonpack: an X11 stand-in and GLX on the firmware's SDL2); sysvsem (System V semaphores for kernels without them). |
| [`box64/`](box64) | box64 0.4.4 patch: wrapped libraries with missing helper libs still initialise. Full fork: [Knifethrower/box64-portmaster](https://github.com/Knifethrower/box64-portmaster). |
| [`unity/`](unity) | `unityport` (first-run converter for Unity 5.x-2019 Linux builds on the device: GLES shaders, ASTC textures, audio, FMOD fixes, timing; plus a survey of a new game), the converter pieces it came from, Unity 4 tools (asset reader without UnityPy, DXT texture halving in C, PlayerSettings transplant, joystick-binding neutraliser), managed-DLL and Steamworks stub patching. |
| [`godot/`](godot) | Godot 4 packs: replace files inside a `.pck`, stub Steam/PlayFab singletons and classes of a Steam build, decode binary GDScript (`.gdc`), list what fills the RAM, load music lazily, run a pack on stock Godot under Xvfb with a memory probe. |
| [`gles/`](gles) | OpenGL 1.x on GLES 2 layers to compile into a game: a C one with lighting, two texture units, fog, texgen and alpha test (Bugdom 2), and a CPU-batching C++ one (Cube). |
| [`allegro/`](allegro) | Allegro 5: a GTK-free native dialog addon stub, and an SDL-backend patch for textures that went blank after the fullscreen resize. |
| [`machismo/`](machismo) | macOS arm64 games via [machismo](https://github.com/bmdhacks/machismo): `macsurvey` (is a Mac build a candidate), a scan for ARMv8.1+ instructions a Cortex-A53 can't run, two machismo patches, a file-read tracer. |
| [`love/`](love) | `fit.lua`: one `require` makes a fixed-window LÖVE 11 game fill any handheld screen (scaling, bars, mouse, pointer); LÖVE 0.10 games on 11.5 (compat shim); notes for PortMaster's love_11.5 runtime. |
| [`re/ghidra/`](re/ghidra) | Headless Ghidra scripts: export a whole program as C, read constants. |

Each folder has a README (or a header comment in each script) with usage.

## Licenses

0BSD unless a folder has its own `LICENSE` file: see [LICENSES.md](LICENSES.md).
