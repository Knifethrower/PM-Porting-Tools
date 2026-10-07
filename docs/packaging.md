# Packaging a port for PortMaster

How a finished port is laid out, launched and submitted: what PortMaster reviewers expect, how to
keep a launcher lean, `port.json`, first-run setup through PortMaster's patcher, Python on the
device, licences and line endings. Building the binaries themselves is on
[building native code](building-native-code.md).

## Start from PortMaster's own rules

PortMaster's documentation is the authority. Read it first and check every port against it before
calling it done:

- [portmaster.games/porting.html](https://portmaster.games/porting.html) and the other pages under
  the site's Contribute tab (packaging, pull requests).
- [PortsMaster/PortMaster-New](https://github.com/PortsMaster/PortMaster-New): the repository ports
  are submitted to. Its `AGENTS.md` is a short, blunt list of what gets a pull request rejected; it is
  written for AI agents but applies to anyone. Its pull request template asks you to confirm you can
  explain every non-standard line of the launcher.
- [PortsMaster/PortMaster-GUI](https://github.com/PortsMaster/PortMaster-GUI): the code behind
  `control.txt`, the `pm_*` helpers, harbourmaster and the patcher. When the docs are silent, the
  answer is in this code. Keep a clone at hand.

This page adds what those sources do not spell out, learned from the author's ports and their
reviews.

## What reviewers reject

PortMaster-New is maintained by people who review every port by hand. As of Oct 2026, AI-built
ports whose launcher is substantially longer or more complicated than comparable merged ports are
rejected without discussion (`AGENTS.md`), and the pull request template asks for a game-specific
reason for any logic merged ports do not have. Concretely:

- **Compare with recent ports of the same kind.** Take the five most recently added ports
  (`date_added` in
  [PortMaster-Info's ports.json](https://raw.githubusercontent.com/PortsMaster/PortMaster-Info/main/ports.json))
  and the newest port of the same type (Westonpack, box64, patcher, Godot, ...). Use their variable
  names (`GAMEDIR`, `CONFDIR`, `BINARY`), their order of blocks and their helpers.
- **No debug knobs.** No `PROFILE=1` switches, no option files, no `config.txt` with commented
  settings, no logging extras for your own testing. A user-facing `config.txt` made support easier
  on an early port, but reviewers treat option files like this as unneeded complexity.
- **No backwards compatibility with your own test builds.** Version migration, "if the old
  layout is there, move it" blocks and version strings in flag files are all unwanted.
- **No env vars that change nothing.** Check the library's defaults in its source before passing a
  variable. Example: gl4es already defaults to `LIBGL_ES=2`, `LIBGL_GL=21` and `LIBGL_SILENTSTUB=1`
  (`src/gl/init.c`), so passing them is noise, while `LIBGL_AVOID16BITS=0` does change something
  (the default is 1 on every GPU except PowerVR). Variables that did nothing in one port: `DISPLAY`
  for a program that never opens a real X display, `BOX64_UNITY`, library folders the program opens
  by absolute path anyway.
- **No defensive re-implementations** of what PortMaster already does (cleanup traps, process
  killing at exit, permission fixing loops, CFW detection tables).
- **Every remaining non-standard line needs a reason** you can give in one sentence.
- **CFW-agnostic.** A script written against one firmware's paths gets rejected. The
  [devices and firmwares](devices-and-firmwares.md) page lists the differences; the testing matrix
  PortMaster-New asks for covers ROCKNIX (Panfrost or libmali), muOS, dArkOS, Knulli and AmberELEC,
  with ArkOS optional.
- **Don't touch anything outside your port** (other ports' metadata, genres).
- **Keep the original porter's credit** when a port builds on someone else's work.
- **Ship patched files, don't patch them at launch.** One port patched a small byte fix into a runtime's gl4es on
  every launch (about 20 launcher lines); that was dropped as too much. The lean form is a pre-patched copy of the
  library shipped in the port, preloaded with one line, with the commands that make it in the
  README's Compile section and its licence in `licenses/`.

The effect is large. Ittle Dew's launcher went from 255 lines to 78 once it was cut down this way.
Write it lean from the start; removing things later costs a device test round each time.

## Release folder layout

The author keeps one folder per port that is exactly what goes into the PortMaster-New pull request,
and builds the zip from it:

```
release/
  Port Name.sh              launcher, title case, may contain spaces
  port.json  README.md  gameinfo.xml  screenshot.png  cover.png
  testing_thread.txt        text for the Discord testing post, not zipped
  portname/                 lowercase letters and digits only (a-z, 0-9): no dashes, underscores or dots
    portname.aarch64        or box64, the runtime binary, ...
    portname.ini            gptokeyb2 mapping
    libs.aarch64/           only libraries the firmwares lack
    gl4es.aarch64/          only if a (patched) gl4es is shipped
    licenses/LICENSE.<component>.txt
    gamedata/PUT_GAME_FILES_HERE.txt   when the user supplies the game
    tools/patchscript       first-run setup, if any
```

Rules that are easy to get wrong:

- `licenses/` is one flat folder inside the port folder, one `LICENSE.<component>.txt` per shipped
  component (see [Licences](#licences)).
- The screenshot is a real capture of gameplay, 640x480. `pm_lint.py` checks 4:3 and the size.
- `gameinfo.xml` paths are as installed on the device: the image is `./portname/screenshot.png`
  (or `cover.png`), not `./screenshot.png`, although the file sits next to `port.json` in the
  repository.
- `port.json` `items` lists only the launcher and the port folder.
- No nested duplicate folders, no `.gitkeep`, no PC test files. Keep sources of your own tools in a
  separate source folder and let the zip builder add them; copies of sources inside `release/`
  drifted from the shipped binary in one port.
- Variants (another CFW, a debug build) are settings or a git branch, never a second release folder.
- Never ship game files, not even converted ones. Games come from the user's own copy and are
  converted on the device (see [First-run setup](#first-run-setup-with-the-patcher)).

## The launcher

### Shape

Every merged launcher starts with the same boilerplate header (find the control folder, source
`control.txt`, `get_controls`). Copy it from a recent port byte for byte; nothing goes above it.
After it, a lean launcher of the author's has these blocks and nothing else:

1. the `mod_${CFW_NAME}.txt` line,
2. `GAMEDIR`, `CONFDIR`, `BINARY`,
3. the patcher block, if the port has a first-run setup,
4. the log redirect (`exec > >(tee "$GAMEDIR/log.txt") 2>&1`),
5. runtime mounts, if any,
6. `chmod +x` of the binaries,
7. gptokeyb2,
8. `pm_platform_helper`,
9. one line that starts the game with its environment,
10. `pm_finish`.

Facts behind some of these lines:

- **Keep the `mod_${CFW_NAME}.txt` line**
  (`[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"`).
  `control.txt` does not source it; it is where each CFW defines `pm_platform_helper` and its
  overrides.
- **Exec bits.** On ext filesystems harbourmaster runs `chmod -R 777` over an installed port
  (`_fix_permissions`); exFAT and FAT have no permission bits at all. Zip extraction elsewhere can
  still drop them, and PortMaster-New's `AGENTS.md` asks for the binary's exec bit to be set at
  runtime, so keep one `chmod +x` line for the binary (and the patchscript, which the patcher
  executes directly). Do not `chmod +x` the `.sh` in the repository: it is committed as 644.
- **End with `pm_finish`.** No `trap`, no `pkill` at the end; EmulationStation's process groups
  handle cleanup.
- **`$GPTOKEYB2` is a command line, not a path.** Never quote it.
- **gptokeyb2 kills the game by name**, and Linux process names (`comm`) are cut at 15 characters.
  A binary called `hocuspocus.aarch64` must be passed as a shorter prefix (`hocuspocus`), or named
  within 15 characters (`domino.aarch64`). Under box64 the name is the x86 binary's, not
  `box64` (Ittle Dew: `IttleDew.x86_64`). The quit hotkey sends SIGKILL, so a game that saves only
  on exit loses progress: make it autosave.
- **Find a process by scanning `/proc/*/comm`**, wherever a launcher or setup script needs a pid
  (thread pinning, waiting for the game). `pkill -x` and `pgrep -x` are unreliable with busybox
  (Knulli, ROCKNIX), and `pkill -f` / `pgrep -f` also match wrapper shells.
- **Never set `SDL_VIDEODRIVER` or `SDL_AUDIODRIVER`, never `export LD_PRELOAD`.** Scope a preload
  to the one command that needs it.
- **Saves.** Find out where the game really writes (its log usually says) and redirect only that:
  `HOME=$CONFDIR` on the game's command line, or `bind_directories`. The second argument of
  `bind_directories` must already exist as a directory, or the call silently does nothing; `mkdir -p`
  that exact path first. Raw symlinks lose saves on exFAT. Check the README's save path against the
  game's log.
- **Runtimes** (squashfs images such as Westonpack or `python_3.11`): fetched with harbourmaster's
  `runtime_check` and mounted by the launcher itself (harbourmaster only downloads them). Skip the
  unmount when `PM_CAN_MOUNT=N`. Copy the mount block from a recent port that uses the same
  runtime. `love_11.5` is different: it ships in PortMaster's control folder
  (`$controlfolder/runtimes/love_11.5/love.txt`), so a LÖVE launcher sources that file and mounts
  nothing ([LÖVE](engines/love.md)).

### Shell rules for launchers

The firmwares' shells differ (busybox tools, a `paste` that is not coreutils); the facts per
firmware are on [devices and firmwares](devices-and-firmwares.md#firmwares). The rules that follow
from them:

- **Never call `paste`** (on ROCKNIX it uploads logs). Join lists with `tr '\n' ',' | sed 's/,$//'`.
- **Never use `nproc`**; count `/sys/devices/system/cpu/cpu[0-9]*`.
- **No `tar -xzf`**; use `gunzip -c file.tar.gz | tar xf -`.
- **Unpack GOG `.sh` installers with PortMaster's 7-Zip**, `"$controlfolder/7zzs.$DEVICE_ARCH"`
  (there is no bare `7zzs`), not busybox `unzip`; most merged ports that unpack installers do. Older
  GOG installers need `-tzip`, or 7-Zip opens the gzip stream of the makeself header and extracts
  nothing.
- **Find processes through `/proc/*/comm`**, not `pgrep` / `pkill` (see above).

## port.json

Use the schema version 4 as merged ports do, and look at
PortMaster-GUI before guessing a field's meaning. Points the generator does not make obvious:

- `runtime` is a list of runtime keys exactly as in PortMaster's runtime catalogue
  (`weston_pkg_0.2`, `mesa_pkg_0.1`, `python_3.11`, `frt_3.5.2`), not the display names. A wrong
  key fails only on a device that does not already have the runtime from another port, so it can
  pass your own tests. `pm_lint.py` checks the keys are given without `.squashfs`. A port that
  mounts nothing has `"runtime": []`; that includes LÖVE ports, since `love_11.5` is in the control
  folder and is not downloaded.
- `reqs` excludes devices. Seen: `!lowres` (no 480x320), `2gb` (no 1 GB devices), `4gb`, `power`,
  `opengl`, `analog_2`, `!trimui`. Set only what the port really needs, and measure memory first
  (see [performance and memory](performance-and-memory.md)).
- `min_glibc` is the newest glibc symbol version any shipped binary needs. Examples: 2.30 for a port
  that uses the `python_3.11` runtime; 2.39 for a box64 built on Ubuntu 24.04 with `-mcpu=cortex-a55`
  (which also limits it to RK3566-class CPUs). How to keep it low:
  [building native code](building-native-code.md).
- `store` entries are objects with `name`, `gameurl` and `developerurl`, not bare URLs (flagged in
  a review).
- `availability` is `"paid"` for a commercial game the user supplies, `"full"` for a free port with
  everything included; `rtr` (ready to run) is true only when nothing has to be added.
- `desc` and `inst` are plain text shown in the PortMaster app; the `_md` variants are for the
  website and only worth filling when they add something.
- Supporting one store's build only is fine (reviewer advice). Say so in `inst` and the README, and
  have the setup detect the other builds by their files and stop with a clear message, since users
  do not read READMEs.

## README, screenshot, testing post

Follow the README layout of recent ports: no top-level title, a thank-you to the authors of what is
ported, the controls, and for anything built from source a Compile section that reproduces the
build from a bare system. Rows only for buttons that do something; no "known limitations" section.
Run the Compile block verbatim in a clean directory once and compare the binary byte for byte with
the shipped one.

The Discord testing post (`testing_thread.txt` in the author's layout) has the controls and the
CFW and resolution checklist. PortMaster expects documented tests on the major firmwares before a
pull request; see [testing and debugging](testing-and-debugging.md) for doing that efficiently.

## First-run setup with the patcher

Game files cannot be shipped, so anything that must change them (unpacking an installer, converting
textures or shaders, byte patches) runs on the device on the first launch. PortMaster's patcher shows
that work in a small GUI with progress and error messages. The launcher exports `PATCHER_FILE` (your
script), `PATCHER_GAME` and `PATCHER_TIME` and sources `$controlfolder/utils/patcher.txt`.

### How the patcher really behaves

From PortMaster-GUI's `utils/patcher.txt`, `utils/patcher/main.lua` and `patch_thread.lua`
(checked Sep 2026):

- `patcher.txt` runs a LÖVE program on the `love_11.5` in the control folder with its own gptokeyb, and
  ends with `pm_gptokeyb_finish`. Merged patcher ports still run `kill -9 $(pidof gptokeyb)`
  right after it, before starting their own gptokeyb2. (This is not the exit cleanup that
  `AGENTS.md` forbids.)
- The script is started with Lua's `io.popen(PATCHER_FILE)`. It needs a shebang and the exec bit,
  inherits the launcher's exported environment (export `GAMEDIR`, `ESUDO`, `controlfolder`,
  `DEVICE_ARCH` and whatever else it uses) and must not rely on the working directory.
- Only **stdout** is shown: the last 12 lines, wrapped at 62 characters. Send tool output (unzip
  warnings, converters) to a log file and print short progress lines.
- **The exit code is ignored.** After the script ends the GUI always reports "Patching completed
  successfully!" and quits. To report a failure, print `PATCH_FAIL_MSG:<reason>` (the on-screen
  text) and then `Patching process failed!`.
- Therefore the launcher cannot trust the patcher's outcome. The script writes a **flag file as its
  very last step**, and the launcher refuses to start without it.
- `PATCHER_QUESTIONS` names a Lua file returning questions; the answers reach the script as
  `NAME=value` environment variables.

### Launcher side

Run the patcher **before** the log redirect (the script keeps its own `patchlog.txt`), only when
the flag file is missing:

```bash
if [ ! -f "$GAMEDIR/gamedata/.patched" ]; then
  if [ -f "$controlfolder/utils/patcher.txt" ]; then
    export PATCHER_FILE="$GAMEDIR/tools/patchscript"
    export PATCHER_GAME="Port Name"
    export PATCHER_TIME="a few minutes"
    export controlfolder ESUDO DEVICE_ARCH GAMEDIR
    source "$controlfolder/utils/patcher.txt"
    kill -9 $(pidof gptokeyb)
  else
    pm_message "This port needs the latest PortMaster to set up the game."
  fi
fi
[ -f "$GAMEDIR/gamedata/.patched" ] || exit 1
```

Compare with a recently merged patcher port (TEOCIDA was the reference for the author's) before
copying this. `.patched` is an example: the flag file's name and the messages are per-port.

### Script side

```bash
#!/bin/bash
cd "$GAMEDIR" || exit 1
fail() { echo "PATCH_FAIL_MSG:$1"; echo "Patching process failed!"; exit 1; }
echo "Unpacking the installer..."
"$controlfolder/7zzs.$DEVICE_ARCH" x -tzip -aoa gamedata/*.sh "data/noarch/game/*" \
  -ogamedata >>patchlog.txt 2>&1 || fail "Could not unpack the installer."
# ... checks, conversion, patches ...
touch gamedata/.patched
```

- **Game and store checks belong here**, as `PATCH_FAIL_MSG`: wrong store version (Steam's
  `libsteam_api.so`, a Windows `.exe`), missing files. Builds that lack the expected executable
  never get the flag, so setup runs again for them and shows the reason each time.
- **Byte patches need no Python**: check the file's `sha1sum`, then `printf '\xNN' | dd conv=notrunc`
  at the offset. Make it idempotent (skip when the patched hash is already there).
- **Larger patches**: an xdelta3 delta against the user's file, applied here. Keep the original as
  `.orig`. For game versions you have no patch for, decide whether they run unpatched (exit 0,
  no failure) or stop; fail only when the patch is known to be needed.
- **Updating a released port**: when a new release must change data the setup already converted,
  keep a patch version in the port (for example `tools/patch/version`) and a copy of what was
  applied next to the data; setup re-runs from the `.orig` when they differ. That is how a timing
  fix and later loading-time patches reached installs that had already run setup. This is for
  released versions, not for your test builds.
- Test the whole thing on the PC with a fake `patcher.txt` that just runs the script and shows its
  stdout: [`fake-patcher`](../devtools/pc-testing/fake-patcher/). Kill it halfway and run it again
  to check that it resumes.

## Python on the device

PortMaster's `python_3.11` runtime (CPython 3.11.8, needs glibc 2.30) is the way to run Python on
the device, for a setup script or a whole app. Mount it like any runtime and set `PYTHONHOME`.

- **Packages**: vendor cp311 manylinux aarch64 wheels, unpacked into a folder of the port on
  `PYTHONPATH`. Pure Python packages can come from the sdist. A Unity asset conversion stack
  (UnityPy with Pillow, lz4, brotli, texture2ddecoder, etcpak, astc-encoder-py and their
  dependencies, `archspec` among them) came to 54 MB.
- **Stub what you must not ship.** `fmod_toolkit` bundles FMOD binaries; UnityPy imports it only for
  audio export, so a stub module replaced it.
- **Optional C accelerators can be left out.** UnityPyBoost (UnityPy's C++ part, needs `-std=c++20`)
  still segfaulted under qemu when built for aarch64; the pure Python path works, more slowly.
- **Expect device conversion to be slow**: Gone Home's, about 1.5 minutes on a PC, was estimated
  at 20 to 40 minutes on the device (not measured). For your own devices a pre-converted copy saves the wait; never
  distribute it.
- The runtime lacks some shared libraries (`libssl`, `libsqlite3`) and `ctypes.util.find_library`
  does not work on the devices. Details and fixes: [apps and video](engines/apps-and-video.md).
- Test it under qemu-user with the runtime extracted on the PC
  ([testing and debugging](testing-and-debugging.md)).

## What to bundle

- Bundle only what the firmwares lack; prove it with `readelf -d` / `ldd` against the CFWs rather
  than copying a distro's libraries. Library bloat is the reviewers' top complaint.
- **Never bundle SDL2** (or SDL2_image, SDL2_mixer, SDL2_ttf), libc, libstdc++, libpthread, librt,
  or GL/EGL drivers and Mesa. Every CFW ships its own SDL2 patched for its display, input and audio.
  Not static either: see [building native code](building-native-code.md#never-link-core-libraries-statically).
- Ship a bundled library as one real file named exactly as the binary's `NEEDED` entry, not a
  symlink chain: symlinks do not survive every zip or copy step.
- Check whether a runtime already provides a library before copying it: one port's `libs.aarch64`
  held byte-identical copies of Westonpack's own libraries, which the runtime already puts on the
  path.
- Never put a `libGL.so.1` in a folder that a Mesa or system GL path will search; see
  [graphics](graphics.md).

## Licences

PortMaster wants a licence file for every component in the port, fonts and assets included:

- One flat `licenses/` folder, one `LICENSE.<component>.txt` per shipped component.
- Include the things you did not write and might forget: box64 (MIT), gl4es (MIT, also for a patched
  copy taken from a runtime), x86_64 `libstdc++`/`libgcc_s` shipped for box64 (GPL with the runtime
  library exception), soundfonts, Python packages (and the libraries bundled inside their wheels,
  such as Pillow's, or Arm's astcenc (Apache-2.0) inside astc-encoder-py), every Rust crate linked
  into a binary.
- Take each licence from the upstream project, not from a packaging layer: one package's bundled
  `LICENSE` covered only a BSD sub-component of an LGPL library.
- Copy them with a script from the sources you built, so a rebuild cannot miss one:
  [`collect_licenses.example.sh`](../build/native-aarch64/collect_licenses.example.sh) does it for a
  port with sixteen components.
- Check the game's own asset licences and the upstream's policy before porting, not at submission:
  see [choosing an approach](choosing-an-approach.md#first-checks-minutes-before-any-work).

## Line endings and text

- **Scripts must have LF line endings.** A launcher or `.gptk` saved with CRLF gives a black screen
  and no log at all, the most common first-timer failure. Files written by Windows tools come out
  CRLF by default.
- Clone reference ports with `git config core.autocrlf false` (or `-c core.autocrlf=false`): CRLF
  inside a cloned reference port's config files broke library paths in one port.
- No em dashes anywhere in port content (README, `port.json`, `gameinfo.xml`); `pm_lint.py` flags
  them.

## pm_zip and pm_lint

The [packaging tools](../packaging/) in this repository do the mechanical checks:

- `pm_lint.py <release folder or zip>` checks `port.json` v4, the unchanged boilerplate header,
  banned launcher patterns, README shape, `gameinfo.xml` paths, the screenshot, LF and em dashes.
- `pm_zip.py <release folder>` builds the installable zip with exec bits set from the file type
  (ELF, shebang, `.so`) and text normalised to LF, leaving `testing_thread.txt` out.
- `repack_launcher.py` swaps one file in an existing zip.

Then run the zip through a launcher simulation
([`pm_sim_test.sh`](../devtools/native-testing/) for native ports) before it goes to a device. A
simulation must install files the way harbourmaster does (permissions included), or it tests
something else. Build scripts should delete the old zip and binaries first: a failed step otherwise
leaves a stale file that looks like a fresh build.

## Before the pull request

- `pm_lint.py` passes; the launcher is no longer than recent comparable ports.
- Tested on the firmwares and resolutions PortMaster asks for, with logs kept.
- No debug aids left in the game either: a debug damage key on Enter was found on a device after a
  release had been built.
- One feature branch per port. Push fixes to the open pull request instead of closing and
  resubmitting, which loses the review history.
- PortMaster-New's CI rejects a launcher or port folder name already used by another port, raw files
  over 90 MB (split them with the repository's `tools/build_data.py`) and a `gameinfo.xml` that does
  not match the files.
