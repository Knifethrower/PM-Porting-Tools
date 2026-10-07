# Devices and firmwares

What differs between the handhelds and custom firmwares (CFWs) PortMaster runs on, as met in the
author's ports: SoCs and CPU cores, GPUs and drivers, RAM and screens, and the firmware quirks that
broke ports. Facts are dated where the source had a date; firmwares change, so re-check on current
releases.

## Devices used in testing

| Device | SoC, CPU | GPU, driver | RAM | Screen | Firmware tested |
|--|--|--|--|--|--|
| Anbernic RG353V | Rockchip RK3566, 4x Cortex-A55 (up to 1992 MHz) | Mali-G52, libmali | 2 GB | 640x480 | dArkOS |
| Anbernic RG351P | Rockchip RK3326, 4x Cortex-A35 | Mali-G31, libmali or Panfrost | 1 GB (981 MiB reported) | 480x320 | ROCKNIX |
| Anbernic RG Cube XX | Allwinner H700, 4x Cortex-A53 at 1.5 GHz | Mali-G31, libmali (fbdev) | 1 GB (995 MiB) | 720x720 | Knulli, muOS |
| TrimUI Brick | Allwinner A133P | PowerVR GE8300 | 1 GB (974 MiB) | 1024x768 | muOS |
| TrimUI Smart Pro | Allwinner A133P, 4x Cortex-A53 at 1.8 GHz | PowerVR GE8300 | 1 GB (974 MiB) | 1280x720 | Knulli (tester device) |
| Retroid Pocket 5 | Snapdragon 865 | Adreno, Mesa | | | ROCKNIX (variant built, never run on the device) |
| Powkiddy X55, Anbernic RG552, Powkiddy RGB30 | | | | | ROCKNIX, AmberELEC (tester runs) |

The RK3566 thermal-throttles from 1992 to 1608 MHz after about a minute of load (RG353V, Sep 2026):
measure after warm-up.

## CPU: write for ARMv8.0

The cores in the field go from Cortex-A35 and A53 (ARMv8.0) to A55 (ARMv8.2). Code built for the
newest core crashes with SIGILL on the oldest: build for `-march=armv8-a`, never `-mcpu=cortex-a55`
([building native code](building-native-code.md#armv80-portability)). Prebuilt binaries from
elsewhere (a macOS arm64 game, a library) may use newer instructions too: Spaghetti Celesti's LuaJIT
died on the RG Cube XX with SIGILL on `eor3` (SHA3). Scan a binary with [scan_isa.sh](../machismo/)
before the first device run.

## GPUs and drivers

Almost every device has a Mali GPU (Bifrost: G31, G52). Allwinner A133P devices have PowerVR. Only
OpenGL ES is offered by the vendor drivers; desktop GL comes either from gl4es (a translation layer)
or from Mesa's Panfrost on ROCKNIX.

| Driver | Where | What to know |
|--|--|--|
| Mali blob (libmali) | dArkOS, ArkOS, Knulli, muOS, ROCKNIX "Mali" | GLES 2/3 only. No S3TC/DXT: ship ASTC or ETC, or let gl4es decode DXT on the CPU. Behaves differently from Mesa in ways a PC cannot show (below). |
| Panfrost (Mesa) | ROCKNIX, selectable per device (reboot) | Desktop GL as well as GLES. Reports GL 3.1; a `MESA_GL_VERSION_OVERRIDE` for programs that ask for more was written for Night in the Woods but never confirmed on a device. |
| PowerVR | TrimUI Brick, Smart Pro | Graphics memory sits in the kernel driver (not in the game's RSS). gl4es picks 16-bit textures by default here only. Showed 1-pixel lines at tile edges where texture coordinates touch the edge of an atlas tile (Ittle Dew, fixed by moving UVs one texel inside). |
| Adreno (Mesa) | Retroid Pocket 5 (not tested) | |

The Mali blob has a fixed cost per draw call, copies whole buffers on `glBufferSubData`, pays ~10 ms
per GPU readback on the Mali-G31, and rejects separate depth and stencil renderbuffers; GPU memory is
system memory, so every texture counts against the device's RAM. Details and numbers:
[graphics: Mali driver lessons](graphics.md#mali-driver-lessons).

The same game can behave differently on the same SoC with another libmali release or another window
system. On ROCKNIX + libmali (Sep 2026) Night in the Woods drew every frame black through Westonpack,
and Ittle Dew's GPU framebuffer copies returned wrong content under Westonpack but were correct
without it ([graphics: LIBGL_NOBGRA](graphics.md#framebuffer-copies-and-libgl_nobgra)). Test each
firmware separately.

## RAM classes

| Class | Devices (examples) | What it means |
|--|--|--|
| 1 GB | R36S, RG351 series, RG Cube XX, TrimUI Brick and Smart Pro | Every "1 GB" device reports a bit less than 1 GiB in `MemTotal` (974-995 MiB seen). The front end stays resident: on the RG351P (ROCKNIX) 580-700 MB were available before the game started, with EmulationStation resident at 113-125 MB. Often no swap. |
| 2 GB | RG353V and most RK3566 devices | Unity games through box64 fit (Night in the Woods: ~840 MB RSS on the RG353V). |

- Use port.json `reqs` (`"2gb"`) to exclude 1 GB devices when a port cannot fit; detect a 1 GB
  device in setup by `MemTotal` < 1.5 GiB.
- Swap differs: the TrimUI Brick on muOS had zram 128 MB plus a 512 MB swap file with
  `vm.swappiness=8`; the RG Cube XX on Knulli and the RG351P on ROCKNIX had none. A README can tell
  users how to turn on zram (Ittle Dew on the TrimUI Smart Pro with Knulli ran out of memory without
  swap and played with 256 MB of zram, Oct 2026).
- `/tmp` is tmpfs (RAM) on Knulli at least: never copy a large game there.

How to fit a game into 1 GB: [performance-and-memory](performance-and-memory.md).

## Screens

Sizes met so far: 480x320, 640x480, 720x720, 854x480, 960x544, 1024x768, 1280x720, 1440x1080,
1920x1152 (RG552, 5:3). Aspect ratios from 1:1 to 16:9; no PortMaster handheld is wider than 16:9.

- port.json `reqs: ["!lowres"]` excludes 480x320 screens.
- The launcher gets the size from `DISPLAY_WIDTH` / `DISPLAY_HEIGHT`.
- A port designed for 16:9 must either fill other shapes (widen or heighten the view) or letterbox
  cleanly. How: [graphics](graphics.md#filling-every-screen-shape), and for Unity 4
  [engines/unity-4](engines/unity-4.md).

## Firmwares

PortMaster runs on all of these, and a PR to PortMaster-New expects test results on ROCKNIX
(Panfrost or libmali), muOS, dArkOS, Knulli and AmberELEC, with ArkOS optional ([packaging](packaging.md)).

| | dArkOS / ArkOS | ROCKNIX | Knulli | muOS | AmberELEC |
|--|--|--|--|--|--|
| Devices seen | RK3566 | RK3326, RK3566 and more | H700, A133P | H700, A133P | RK3326 (RG351V), RK3399 (RG552, prerelease) |
| Display path | SDL2 KMSDRM | Sway (Wayland); SDL2 uses Wayland | SDL2 `mali` fbdev driver (only `mali` and `dummy`) | not recorded (no GBM on the TrimUI Brick) | not recorded |
| GL | libmali | libmali or Panfrost (user's choice) | libmali | libmali / PowerVR | Mali |
| Audio | ALSA dmix | PulseAudio / PipeWire | PipeWire | PipeWire (ALSA plugin) | not recorded |
| Runs the port as | user `ark`, game via `sudo` (`$ESUDO`) | root | root | root | not recorded |
| Shell tools | GNU, Info-ZIP `unzip` | busybox for several tools | busybox; `ps` prints only the process name | busybox for several tools | not recorded |

Never set `SDL_VIDEODRIVER` or `SDL_AUDIODRIVER` in a launcher: each firmware chooses its SDL backend
in PortMaster's control files before the launcher runs, and forcing one broke ports on other
firmwares. Use the firmware's own SDL2 (never bundle `libSDL2`); that is also what makes one binary
work on fbdev, KMSDRM and Wayland alike.

### Shell tools

What a launcher's shell commands met per firmware. The launcher rules that follow from these are on
[packaging](packaging.md#shell-rules-for-launchers).

| Trap | Where | Fact |
|--|--|--|
| `paste` | ROCKNIX | `paste` is the system's log-upload command, not coreutils `paste`: a launcher that joined a list with `paste -sd,` posted to a pastebin. `tr '\n' ','` works everywhere. |
| `nproc` | ROCKNIX | Does not exist; count `/sys/devices/system/cpu/cpu[0-9]*`. |
| `pgrep -x` / `pkill -x` | ROCKNIX (busybox) | `pgrep -x` matched nothing for a box64 game, so thread pinning and monitors silently did nothing. Scanning `/proc/*/comm` works on every firmware. `pkill -f` matches wrapper shells and your own SSH command line. |
| `ps` | Knulli | Prints only the 15-character process name, so command-line matching does not help. |
| `unzip` | ROCKNIX (busybox; probably muOS and Knulli too) | Fails on GOG `.sh` installers with `short read`: it expects a local header at byte 0. Info-ZIP `unzip` (dArkOS) copes. PortMaster's own 7-Zip works everywhere. |
| `tar -z` | muOS (busybox) | No `-z`: `gunzip -c file \| tar xf -`. |
| `grep --line-buffered`, `sed -u` | muOS, Knulli (busybox) | Flags missing; read line by line in a shell loop instead. |

### dArkOS / ArkOS

- **Audio**: the ALSA default is plug -> softvol -> dmix at 44100 Hz with 1024-frame periods. The
  hardware is held by EmulationStation and fluidsynth, so opening `hw:` directly fails (FMOD:
  `ERR_OUTPUT_INIT`). Match the rate instead of asking for the device.
- **Launcher as one user, game as root.** With `fs.protected_fifos=1`, the kernel refuses an
  `O_CREAT` open of a FIFO in a sticky world-writable directory (`/tmp`) that belongs to another
  user. A Unity player started by `sudo` could not open a log FIFO the launcher made, and silently
  logged elsewhere. Put such FIFOs in their own non-sticky 0777 directory. ROCKNIX runs everything as
  root, so it did not happen there.
- **`$ESUDO` drops exported variables**: pass them as `env VAR=value` arguments.
- **SDL2 signal handlers**: ArkOS-family SDL2 builds replace the crash handlers of the process at
  `SDL_Init`, which kills box64 games that load SDL2 natively: [box64](box64.md#signals-and-native-libraries-in-the-box64-process).
- **Hotkey daemon**: `ogage` once got stuck at 100% CPU and cut a game's frame rate by a third for
  several runs (RG353V: title 58 -> 45 fps). Log `top` at startup when chasing frame rates.

### ROCKNIX

- **Two GPU drivers.** Players choose libmali or Panfrost (needs a reboot). Westonpack checks
  `glxinfo`: with Panfrost it bypasses Weston and runs the program on the desktop's Mesa GL; with
  libmali it runs crusty in Wayland mode. Test both.
- **gl4es**: the ROCKNIX RK3326 image of 2026-09-01 has no system gl4es (`/usr/lib/gl4es/libGL.so.1`
  missing; the package is only in the build recipes), and `/usr/lib/libGL.so.1` is a 0-byte stub
  there. Bring your own.
- Busybox and missing tools: see [Shell tools](#shell-tools).
- ROCKNIX manages CPU clocks itself, and core layouts vary across its many devices: do not set
  governors or pin threads there.
- The home directory is `/storage`.

### Knulli

- **No System V IPC on the H700 kernel.** Knulli's H700 kernel (4.9.170, and muOS 2502 PIXIE's) is
  built without `CONFIG_SYSVIPC` (Knulli's public config, checked 2026-09-28; also no
  `POSIX_MQUEUE`). `semget` returns ENOSYS. The Unity 4 player (through its Mono) retries forever:
  100% CPU on one thread, no window. Fix: `MONO_DISABLE_SHM=1` (Unity 4's libmono honours it and
  then makes no System V calls; confirmed on the RG Cube XX on 2026-10-04: title in 12 s, 60 fps), or
  the [sysvsem](../shim/sysvsem/) preload for a libmono without that variable. Read
  `/proc/config.gz` on the device before assuming any kernel feature on a vendor-BSP kernel; the
  firmware version says nothing about it.
- **Audio**: PipeWire, socket in `/var/run/pipewire-0`; the front end launches ports with
  `XDG_RUNTIME_DIR=/var/run`. Westonpack sets its own `XDG_RUNTIME_DIR` when none is set, after
  which the ALSA plugin cannot find PipeWire: capture the real value before `westonwrap.sh` and pass
  it back on the wrapped command line.
- **No libgbm**: Westonpack fakes a DRM device. Programs that go through SDL2 are not affected.
- **Data partition** (`/userdata`, including root's home `/userdata/system`) is exFAT through FUSE:
  no permission bits, and a write per sample through it distorts profiling (run test builds from
  `/tmp`). Dropbear refuses `authorized_keys` there.
- gptokeyb2's quit hotkey kills by the 15-character process name: keep binary names at most 15
  characters.
- gptokeyb2 on Knulli did not know `mouse_movement_up` and friends for the D-pad (Oct 2026).
- The image has `strace`, `gdb` and `/proc/config.gz`; the framebuffer `/dev/fb0` holds the real
  scanout (2 pages), good for screenshots.
- SDL2 2.30 and SDL2_ttf 2.24 on Knulli (Oct 2026), newer than a bullseye build sysroot: guard newer
  API calls.

### muOS

- **PipeWire through the ALSA plugin can hang Unity/FMOD audio.** On the TrimUI Brick (muOS 2601)
  Ittle Dew hung 2 of 3 times entering the world (worker threads in futex waits, one thread spinning
  `ppoll`); a null ALSA pcm never hung. `PIPEWIRE_LATENCY=1024/48000` fixed it. Only one device was
  tested, so whether this is the chip or the firmware is open.
- **Kernel features differ between releases**: muOS 2502 PIXIE on the RG Cube XX had SYSVIPC off
  (same hang as Knulli); 2601 rebuilds the kernel, state unknown. A player that hangs before its
  first window is a firmware or kernel question first.
- Ships a gl4es in `/usr/lib/gl4es`; Westonpack ignores it in favour of its own.
- The TrimUI Brick on muOS reports `DEVICE_HAS_ARMHF=N`: no 32-bit userland there.
- Ports live in `/mnt/mmc/ROMS/Ports` (`/mnt/union/...` is a union view of it).

### AmberELEC

- On an RG351V (RK3326, Mali), `SDL_CreateTexture` with a streaming format failed with "Texture
  format not supported" right after the window and renderer were created; on desktop Linux the same
  code works. Games that create their own `SDL_Renderer` and stream pixels may need the software
  renderer there.

## The launcher's view of the device

PortMaster's `control.txt` and the firmware's `mod_<CFW>.txt` set these before the port's own lines
run (summary; see PortMaster's docs for the full list):

| Variable | Content |
|--|--|
| `DEVICE_ARCH` | `aarch64`, `armhf` or `x86_64` |
| `DEVICE_CPU` | SoC id (`rk3326`, `rk3566`, `h700`, ...) |
| `DEVICE_RAM` | RAM in GB |
| `DISPLAY_WIDTH`, `DISPLAY_HEIGHT` | Screen size |
| `CFW_NAME` | Firmware id |
| `ESUDO` | `sudo`, `doas` or empty |
| `PM_CAN_MOUNT` | `Y` or `N`: squashfs runtimes can be mounted |

Log the firmware name and version, kernel, glibc and GL renderer at startup: they explain most
"works on one device, not on another" reports ([testing-and-debugging](testing-and-debugging.md)).

## Old glibc

Firmwares ship older glibc and libstdc++ than a current desktop. Build against glibc 2.28-2.31
(Debian buster or bullseye) and never link libc, libstdc++ or libgcc statically (PortMaster-New
does not accept it). PortMaster's `python_3.11` runtime needs glibc 2.30. Set port.json
`min_glibc` to the newest version any shipped binary needs. How: [building-native-code](building-native-code.md).
