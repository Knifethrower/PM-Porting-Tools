# Testing and debugging

How to test a port without a device, how to test on your own handheld over SSH, which logs to ask
testers for and how to read them, and how to find hangs and crashes on a device that has no
debugger. Measuring speed and memory is on [performance and memory](performance-and-memory.md).

The tools named here are in [`devtools/`](../devtools/); each folder has a README with usage.
Most of them assume a Windows PC with WSL (Ubuntu 24.04).

## Testing without a device

A PC catches launcher, packaging, setup and most rendering mistakes before a tester sees them. It
cannot show performance, driver-specific bugs or software floating point (see the end of this
section).

### aarch64 binaries under qemu-user

- Run aarch64 code on the PC with `qemu-aarch64 -L /usr/aarch64-linux-gnu -E VAR=...`; add target
  libraries the cross toolchain lacks (e.g. zlib from a Debian arm64 package) through
  `LD_LIBRARY_PATH`, or run inside the Debian bullseye arm64 chroot from
  [build/native-aarch64](../build/native-aarch64/), which is also how the release binaries are
  built (see [building native code](building-native-code.md)).
- PortMaster's own runtimes run there too: a setup script ran under qemu-aarch64 with PortMaster's
  python_3.11 squashfs in 63 s (13 s natively on the PC). Test first-run converters this way before
  shipping, and boot the data they produce in the real game.
- **qemu-user does not translate evdev ioctls** (`EVIOCGID` returns ENOTTY), so SDL never sees a
  gamepad under it. For input tests, build the same program natively for x86_64 and drive it with a
  virtual pad ([`uinput_pad.py`](../devtools/device/) works in WSL as root; fake the udev entry
  `/run/udev/data/c13:<minor>` with `E:ID_INPUT_JOYSTICK=1`).
- Converters with a tight memory budget: run one asset file through the aarch64 stack under qemu
  and compare the output with the PC's (images by PSNR, binaries with `cmp`).

### A display: Xvfb

- Run games on **Xvfb with a window manager** (openbox). Without one, keyboard and mouse do not
  reach some games and SDL does not apply fullscreen, so letterboxing looks broken. Alternatively,
  create the window at the `SDL_GetDesktopDisplayMode` size when fullscreen: then no window manager
  is needed and devices behave the same.
- Size Xvfb to the device's screen when the game forces fullscreen.
- WSLg broke the Unity 4 player (dialog crash, then "Display is invalid"); Xvfb + openbox, or a
  headless `weston --xwayland` (closest to Westonpack, but its root window cannot be captured),
  work.
- **WSL kills Xvfb when the `wsl` call that started it ends**, even with `setsid nohup`: run X and the
  game as one long-lived background script.
- **Share the PC carefully.** If other jobs on the same machine use Xvfb, start yours on a free random
  display from a renamed copy of the binary, never `pkill` anything you did not start, and never
  `pkill -f` a pattern that is also in your own command line (it kills your own shell; use
  `pkill -x` with the 15-character process name, or a `[p]attern`).
- Screenshots and clicks on Xvfb: `xwd -root | convert`, xdotool. Under low FPS, hold mouse buttons
  ~0.8 s; `mousemove_relative` works for mouse-look. Examples in
  [pc-testing](../devtools/pc-testing/).

### GLES and gl4es on Mesa

The handhelds have only OpenGL ES. Test the same path on the PC:

- **A game converted to GLES**: an x86_64 `LD_PRELOAD` that hooks `dlsym` / `glXGetProcAddressARB`
  and turns every GLX context into an ES 3.2 one runs a converted Unity game natively on Mesa
  (`gles_force.c` in [pc-testing](../devtools/pc-testing/)). Mesa llvmpipe has ES 3.2 and ASTC.
  `LD_PRELOAD` paths cannot contain spaces: copy the library to `/tmp`.
- **A game through gl4es**: on the PC gl4es runs in a no-test mode that reports almost no
  capabilities; use the Mali profile in [gl4es-mali](../devtools/pc-testing/gl4es-mali/) before
  chasing any gl4es rendering bug. Details, and [gl4es-test](../devtools/gl4es-test/) for gl4es
  itself: [graphics: testing gl4es on the PC](graphics.md#testing-gl4es-on-the-pc).
- mpv refuses llvmpipe unless the test's `mpv.conf` has `gpu-sw=yes`.

### A fake PortMaster

- [`pm_sim_test.sh`](../devtools/native-testing/) runs a port zip's launcher against a fake
  PortMaster control folder (control.txt, libgl files, 7zzs), with the zip unpacked the way the
  installer leaves it (`chmod 777`) and the binary under qemu-user on Xvfb. It shows `log.txt` and
  the port folder after each run. A launcher simulation must install files the way PortMaster does,
  or it tests the wrong thing.
- [fake-patcher](../devtools/pc-testing/fake-patcher/) mimics PortMaster's patcher for first-run
  setup scripts (it prints the script's output and always reports success, like the real one).
- **Test the installer end to end**: a fresh copy of the original files, compare the result with a
  known-good build by hash, then kill it mid-run and check that it resumes. Fake a 1 GB device with
  `unshare -m` and a bind mount over `/proc/meminfo`. Use busybox-like stand-ins where the device
  lacks a tool (7zzs as `unzip`).
- **Prove removals on the PC before the device**: run old and new on the real game files and `cmp`
  the outputs, then one clean install and play on the device. The same habit for binaries
  (rebuilding from the README verbatim, comparing byte for byte) is on
  [building native code: build hygiene](building-native-code.md#build-hygiene).

### Deterministic frames and diffs

For native ports, [native-testing](../devtools/native-testing/) has scripts that run a build with
the `glcount` probe in fixed-clock mode (every frame advances game time by exactly 16 ms, the wall
clock is frozen, random seeds are fixed) with scripted keys, dump chosen frames, and diff two runs
pixel by pixel. Uses:

- **Verify a renderer change** against the previous build: counts of pixels beyond 1, 2 and 5 levels
  and zoomed crops. On Torus Trooper the diff caught a lost tunnel slice that no screenshot review
  would have.
- **Every screen shape at once**: one contact sheet per handheld resolution (640x480, 720x720,
  480x320, 854x480, 960x544, 1280x720 ...) to check field of view and HUD anchoring.
- The same for the aarch64 release binary inside the chroot (slow: few frames).
- Fixed clocks have limits: games that time frames with `std::chrono` (realtime clock) see zero
  deltas.
- For a game you cannot rebuild, a scripted "new game -> gameplay -> mean brightness of the world
  area" check made each gl4es experiment a one-liner (Ittle Dew; `test_world.sh` in
  [gl4es-mali](../devtools/pc-testing/gl4es-mali/)).

### What the PC cannot show

- Speed: x86 runs `long double` and D's `real` in hardware; aarch64 does it in software
  ([native ports: D programs](engines/native.md#d-programs)).
- Driver behaviour: the Mali blob's cost per draw call, its copy of a whole VBO on
  `glBufferSubData`, wrong pixels from some GPU copies of the window surface
  ([graphics: Mali driver lessons](graphics.md#mali-driver-lessons)).
- Memory: llvmpipe stores textures differently from the devices.
- Bugs the x86 build hides (unsigned `char`, uninitialised memory, clang traps):
  [building native code](building-native-code.md#bugs-that-only-appear-on-arm).

## Testing on your own device over SSH

The firmwares used here (ROCKNIX, Knulli, muOS, dArkOS) can run an SSH server; turn it on in the
firmware's settings. Switching GPU drivers and rebooting stay manual steps on the device.

### One connection for everything

- **With keys** (ROCKNIX, muOS; on dArkOS log in as `ark`, keys untested): `ssh-keygen -t ed25519`, then append the public key to
  `~/.ssh/authorized_keys` on the device (Windows has no `ssh-copy-id`; pipe the key into
  `ssh root@<device> "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 700 ~/.ssh && chmod
  600 ~/.ssh/authorized_keys"`). ROCKNIX's home is `/storage`, muOS's `/root`.
- **Knulli cannot use keys**: root's home `/userdata/system` is on the exFAT data partition (FUSE,
  no permissions), so Dropbear rejects `authorized_keys` there, a copy in `/root/.ssh` is never read,
  and this Dropbear (2024.86) has no `-D` option to point it elsewhere.
- **Use one shared connection (OpenSSH ControlMaster)** instead of logging in per command. Open it
  once per boot in a terminal of your own, type the password there, and leave the window open:

      ssh -M -S /tmp/device.sock -N -o ServerAliveInterval=30 root@<device>

  Every script then runs through it with no credentials:
  `ssh -S /tmp/device.sock -o BatchMode=yes root@<device> '<command>'`, files with
  `... "cat > /path" < local` and `... "cat /path" > local`, scripts with
  `... "bash -s <args>" < script.sh`. It lasts until either side reboots or the window closes.
  Windows' own OpenSSH has no connection sharing: run it in WSL.
  [`dev.sh`](../devtools/device/) wraps this.
- **Without any login (Knulli)**: the guest SMB share `\\<device>\share` is the data partition
  (`roms\ports\<port>` is the port folder, `system` is root's home with `logs\`). Enough to install
  a test build and read `log.txt`; launch the port from the menu.
  [`install_smb.py`](../devtools/device/) unpacks a port zip onto it and checks sizes and hashes. It
  is also faster than the SSH pipe for big copies (~0.45 vs ~0.25 MB/s on the RG Cube XX).

### Sharing the device

A test device is often also someone's handheld. **Ask before stopping the front end, rebooting or
killing a game on a device someone else is using.** Keep backups of anything you replace (on the
device and on the PC), and verify every restore by SHA-1.

### Starting a port from SSH

A port started over SSH must not compete with the front end for the screen: on ROCKNIX a game
started while EmulationStation holds the display crawls at 1 fps.

- **Stop the front end, then launch**:
  - ROCKNIX: `systemctl stop essway.service` (sway keeps running). Copy the session environment from
    EmulationStation first (`tr '\0' '\n' < /proc/$(pgrep -o emulationstation)/environ`:
    `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR`, `SWAYSOCK`, `SDL_*`, `HOME=/storage`).
  - Knulli: `/etc/init.d/S31emulationstation stop`; launch with `HOME=/userdata/system
    XDG_RUNTIME_DIR=/var/run` (PipeWire's socket is `/var/run/pipewire-0`).
  - Start the launcher detached and return at once:
    `setsid nohup bash "./<Port>.sh" > /tmp/run.txt 2>&1 < /dev/null &`. Loading can take 1-2
    minutes; poll the log from separate calls instead of a long sleep.
  - Start the front end again afterwards.
  - [`device_run.sh`](../devtools/device/) does start, status (RSS, threads, MemAvailable, log tail)
    and stop for Knulli/Batocera, dArkOS and ROCKNIX; [knulli_tmp](../devtools/device/knulli_tmp/)
    has a Knulli test loop.
- **Pause instead of stopping** for profiling runs: `killall -STOP emulationstation` and `-CONT`
  after. It keeps the audio device, so such a run has no sound.
- **Launch through the front end itself** on Knulli/Batocera: EmulationStation's local API
  (`curl -X POST -d "<rom path>" http://127.0.0.1:1234/launch` on the device) takes the same path as
  the menu. A new port needs `curl 127.0.0.1:1234/reloadgames` first.
- muOS has no `grim` and its launch is easiest from the menu; sample over SSH while it runs. muOS
  ports live in `/mnt/mmc/ROMS/Ports` (`/mnt/union/...` is a union view).

### Input, screenshots and repeatable sessions

- **Scripted input**: [`uinput_keys.py`](../devtools/device/) presses keys on a virtual uinput
  keyboard (the mechanism gptokeyb uses); [`uinput_pad.py`](../devtools/device/) makes a virtual
  SDL gamepad and prints the environment that maps it and hides the real pad
  (`SDL_GAMECONTROLLER_IGNORE_DEVICES`). Source that environment in a `sed`-edited copy of the
  launcher. Menus that fade or load drop presses: send one action, take a screenshot, then send the
  next.
- **Screenshots**: `grim` on ROCKNIX (with the session's `WAYLAND_DISPLAY` and `XDG_RUNTIME_DIR`),
  `/dev/fb0` where the firmware exposes the scanout (Knulli/Batocera; [`fbshot.sh`](../devtools/device/),
  [`fbburst.sh`](../devtools/device/) for a series). Verify screen-shape fixes this way: the PC's
  untouched framebuffer is black anyway.
- **Frame sequences and crops beat glances.** A screenshot every 3 s from the first scene and
  fixed-position crops compared side by side found black intro pages and a missing player sprite
  that timed single screenshots had missed. A dialog scene with a big portrait can pass a glance
  while the small sprite is gone.
- **One variable at a time**: restart the port with one extra variable through a `/tmp` copy of the
  launcher, wait for a log line that marks the scene, screenshot, fetch. This found in two runs
  what weeks of logs had not (a gl4es setting that turned the world black on one driver).
- **Skip the menus in a test build**: a managed-code patch that makes the first scene load save
  slot 1 through the menu's own code got Ittle Dew to gameplay in ~55 s on the RG351P. Never ship it;
  restore the original file after testing.
- [`examples/`](../devtools/device/examples/) has complete sessions: a map-tour benchmark, a
  step-by-step driving session with fps per step, a copy-run-fetch screenshot loop.

### Device-side gotchas

- `pkill -f <name>` over SSH also matches (and kills) the remote shell whose command line contains
  that name. Find the game by `/proc/*/comm` instead.
- **Never overwrite a script or library that is running.** bash reads a script incrementally, and a
  mapped `.so` changes under the game. Upload to a temporary name and `mv` it over the old one (a new
  inode), or stage a new file.
- **Before redeploying a binary, kill every process whose `/proc/*/exe` is that binary.** A leftover
  instance gave "Text file busy", kept the old binary running and drew over the new one.
- A front end restarted in the background by one test can come back over the next game: stop it
  again right before a screenshot.
- Run test copies from `/tmp` (RAM) on Knulli; the data partition is exFAT through FUSE. But `/tmp`
  is RAM: put large games on `/userdata`.
- Binary writes over SSH as piped bytes, not inline `printf` (quoting ate the backslashes).
- Keep an integrity script that checks (and repairs) every byte patch of a test install after a run.

## Logs to ask testers for

Ask for `log.txt` (the launcher's output) and the engine's own log (for Unity ports, the file
passed to `-logFile`, e.g. `unity.log`), plus a screenshot when the picture is wrong. Delete old
engine logs at launch so stale files do not get sent with bug reports.

### What a launcher should log

- The firmware name and version, device, kernel and glibc lines. **A hang before the first window
  is often a firmware question, not a port bug**: read these lines first.
- For box64 ports through Westonpack: the westonwrap mode line, crusty's SDL attributes, the GL
  shim's binding summary, box64 crashes.
- A startup snapshot when debugging performance: uptime, load, SoC temperatures, `top`.
- The engine log carries the renderer and version, the extension list, shader failures, scene names
  and audio errors. Engine-specific lines to look for are on
  [Unity: logs to ask for](engines/unity.md#logs-to-ask-for) and
  [Unity 4: logs to ask for](engines/unity-4.md#logs-to-ask-for).

### Reading a tester's log

- **Read the session's shape first.** Exit 137 right after `back`/`start` presses in the gptokeyb log
  is a Select+Start quit (SIGKILL), not a crash. The first and last timestamps give the session
  length. A line that marks the world as loaded tells how far the game got.
- **"Aborted" at the end of `log.txt` after an in-game Quit** can be the engine crashing during its
  own shutdown (see the case below), harmless to the player.
- A message repeated thousands of times usually has one cause upstream: Unity 4's
  `An invalid object handle was used` came from every sound call after `FMOD failed to initialize`.
- **Lines from different writers are not in time order.** Buffered stderr lines (an fps preload)
  and the engine's own lines land in the log in a different order from when they were written:
  select a phase by run timing, never by line position.
- **Unity's `-logFile` takes over stderr**, and Unity's writes overwrite lines from anything else
  writing to stderr. Send diagnostics from injected libraries to their own file (`O_APPEND`, one
  `write()` per line).

### Keeping one log file intact

- Have only `tee` write `log.txt` (`exec > >(tee log.txt) 2>&1` in the launcher): everything else
  writes into the pipe that feeds it, and each line under `PIPE_BUF` (4 KB) is one atomic write.
  A stress test of 5000 + 5000 lines from two writers kept all 10000 intact.
- Logging is test tooling: tester builds that differ in one launcher line each (fps logging on,
  one setting changed) beat options in the release. Keep the release launcher lean (see
  [packaging](packaging.md)).

### Filtering a noisy log through a FIFO

When a library floods the log (Westonpack's gl4es printed a debug line on every `glReadPixels` and
`glCopyTexSubImage2D`, ~3000 lines a minute in Ittle Dew), a live filter can keep it readable without
changing the library:

- Make the engine's log path a FIFO and start a reader **before** the game, or the engine blocks
  opening it. Create it in a directory of its own, not straight in `/tmp`
  (`mkdir -m 777 /tmp/<port>-log && mkfifo -m 666 /tmp/<port>-log/log.fifo`): see the last point.
- Read it with a bash loop (`while IFS= read -r -u 3 line || [ -n "$line" ]; do ...; done`), not
  `grep --line-buffered` or `sed -u`: busybox builds (muOS, Knulli) lack those flags.
- Hold the FIFO read-write (`exec 3<>"$FIFO"`), so a writer that closes and reopens it cannot cause a
  false end of file. Then there is no end of file at the end either: after the game, write a stop
  marker line into the FIFO and break on a line *ending* with it (the engine's last line may lack a
  newline). Kill the reader if it has not stopped after a few seconds, and remove the FIFO.
- If `mkfifo` fails, fall back to a plain log file.
- **A FIFO in `/tmp` fails when launcher and game run as different users.** On dArkOS the launcher
  runs as `ark` and the game as root, and `fs.protected_fifos=1` refuses the player's reopen of a
  FIFO in a sticky world-writable directory that another user owns: only the player's first 4 lines
  arrived, the rest went to Unity's default `Player.log`. ROCKNIX runs everything as root, so the
  same launcher worked there. Listing the game's open files (`ls -l /proc/<pid>/fd`) showed it.
  Use a non-sticky 0777 directory of its own
  ([devices and firmwares: dArkOS / ArkOS](devices-and-firmwares.md#darkos--arkos)).
- Ittle Dew in the end dropped the filter: the two debug calls were patched out of the gl4es copy
  it already patched for another bug ([graphics](graphics.md)).

## Hangs

- **Sample the threads**: per thread from `/proc/<pid>/task/*`, CPU ticks over 5 s (fields 14 + 15
  of `stat`), state, `wchan`, `syscall` (number and arguments), and `/proc/<tid>/fd`. Every thread
  in `futex_wait` or `pipe_read` is a deadlock or a wait on something outside; one thread at 100%
  is a spin. [`thread_sample.sh`](../devtools/device/) samples per-thread CPU ticks, state and
  `wchan`, plus the major-fault rate that tells thrashing from a hang, without gdb; read `syscall`
  and the fds by hand.
- A syscall argument that is a pointer can be read from `/proc/<tid>/mem` (the `pollfd` of a `ppoll`
  tells which fd it waits on).
- Under box64 the main thread can be a zombie (`Z`) leader that hides `/proc/<pid>/fd`; that is
  normal, read the threads' fds instead.
- **Always `strace` a spinning process when the firmware has strace** (Knulli does). Sampling said
  only "running, no fds" for the Unity 4 player on the H700; `strace -c -f -p <pid>` for a few
  seconds showed 147k `semget = ENOSYS`, and `zcat /proc/config.gz | grep SYSVIPC` showed the kernel
  was built without System V IPC. The fix and the kernel status are on
  [devices and firmwares](devices-and-firmwares.md).
- Before assuming a kernel feature, read `/proc/config.gz` on the device (Knulli and muOS ship it);
  the firmware version number says nothing about it.
- **Reproduce firmware differences on the PC with fault injection**:
  `strace -f -e inject=semget,semop,semctl,semtimedop:error=ENOSYS` on the native player reproduced
  the H700 hang on a PC, and proved the fix there.

## Crashes

### Native programs

[`segvtrace.c`](../devtools/device/) is an `LD_PRELOAD` that prints the signal, fault address, a
backtrace and the executable mappings on SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT and SIGTRAP into
`log.txt`, then re-raises. Resolve the addresses on the PC against the unstripped binary with
`llvm-addr2line -f -C -e <binary> <offset>` (offset = address - mapping start + mapping offset).

### Games under box64

- Run with `BOX64_LOG=1 BOX64_SHOWSEGV=1 BOX64_SHOWBT=1` and **read `si_code` first**. Example:
  `si_code -6` (SI_TKILL) with the PID as "address" at Mono's first JIT write meant the firmware's
  SDL2 had replaced box64's SIGSEGV handler
  ([box64: signals](box64.md#signals-and-native-libraries-in-the-box64-process)).
- Mono's own GC signals (30 and 24) appear in every run with `BOX64_SHOWSEGV=1` and are normal.
- Mono's "native stacktrace" in a crash report is often only its own crash-report code. The register
  dump is what tells.

### Case: a crash report that the player never saw

On the author's RG Cube XX (Knulli, Oct 2026), some Ittle Dew launches from the menu left Mono's
crash report and "Aborted" in the logs, while the game had run fine every time. How it was settled, as a method:

1. **Ask what the player saw.** The game always launched and ran; the reports were written around
   quitting. That moved the search from "crash during play" to "crash at exit".
2. **Check what a harness actually tests.** About 64 automated launches never produced a report,
   but the scripts checked the log *before* closing the game, and a Select+Start quit (`pkill -9`)
   kills the process before the engine's teardown. Only the game's own Quit menu reached the code
   that crashed. A comparison of box64 settings that looked significant (~4 of 10 sessions vs 0 of
   8) was an artefact of how those sessions ended.
3. **Read the register dumps against the binary.** Both dumps were the same instruction at the same
   stack depth: a virtual call through slot 25 of a vtable while a GameObject walked its component
   list during teardown. The vtable pointer pointed at a Shader's 22-slot table in one run (slot 25
   was the text " differe" of the following string: that was the garbage instruction pointer) and at
   an AnimationClip's in the other (slot 25 was zero). Two different wrong classes in one slot is a
   stale pointer into freed and reused memory, not a stack overwrite.
4. **Check whether anything is lost.** Unity had written its prefs a second before, and the game
   saves in `OnApplicationQuit`, which runs before the teardown. The report is cosmetic.

Lesson: confirm when a report is written and what the player saw before changing settings, and make
test harnesses end the game the way players do.

## Further reading

- [Performance and memory](performance-and-memory.md): measuring fps, profiling, memory logging.
- [Graphics](graphics.md): gl4es and driver behaviour behind most picture bugs.
- [Devices and firmwares](devices-and-firmwares.md): which firmware has which tools (`strace`,
  `grim`, busybox) and kernel features.
- [Packaging](packaging.md): what goes into the release and what stays in test builds.
