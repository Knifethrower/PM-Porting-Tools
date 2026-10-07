# Building native code for the handhelds

How to build aarch64 binaries (games, libraries, shims, tools) that run on every current PortMaster
firmware: which glibc to target, the Debian bullseye chroot and the clang cross build, why core
libraries stay dynamic, ARMv8.0 portability, and how to check a binary before shipping it. Where it
goes in the port and how `min_glibc` is set: [packaging](packaging.md).

## Target an old glibc

A binary needs at least the glibc version of every symbol it links against. Firmwares ship
different, sometimes old, glibc versions, so a binary built on a current desktop distribution does
not start on many of them.

- Ubuntu 24.04's `aarch64-linux-gnu-gcc` links against glibc 2.39. A box64 built with it needed
  `min_glibc: "2.39"`, and the release that used it was limited to the RG353V on dArkOS.
- **Debian 11 bullseye** (glibc 2.31, GCC 10, libstdc++ up to GLIBCXX_3.4.28) runs on every current
  PortMaster CFW, and is what the author's native ports build against. A binary built there usually
  needs much less than 2.31: Hocus Pocus needed glibc 2.17 and GLIBCXX_3.4.26, the glxsdl shim
  GLIBC_2.27 and GLIBCXX_3.4.11.

Routes that work, and when to use them:

| Route | glibc | Use for |
|--|--|--|
| Build inside a bullseye arm64 chroot (GCC 10 under qemu-user) | 2.31 | anything with C++, autotools, builds that run their own tools; slow (emulated compiler) |
| Host clang + lld, bullseye chroot as sysroot | 2.31 | big C++ or C projects at native speed |
| Ubuntu's cross gcc with a Debian 10 sysroot and explicit include paths | 2.28 | plain C only (box64, small shims) |
| Ubuntu's cross gcc with a Bootlin glibc 2.35 sysroot | 2.35 | seen in one port; runs on fewer firmwares |

The chroot, a `chroot.sh` wrapper and a CMake toolchain file for the cross build are in
[`build/native-aarch64`](../build/native-aarch64/); its README has the setup commands.

### The bullseye chroot

`debootstrap --arch=arm64 bullseye` plus `qemu-user-static` and binfmt gives a Debian arm64 system
that runs on an x86 PC; install `build-essential`, `cmake` and the game's `-dev` packages inside.
Mesa in the chroot also gives a software GLES for PC tests under qemu
([testing and debugging](testing-and-debugging.md)).

- **Bullseye is archived (Sep 2026).** Debian 11 left LTS and its packages moved to
  `archive.debian.org`; `deb.debian.org` returns 404 for files its indexes still list. Point the
  chroot's `sources.list` at `http://archive.debian.org/debian` (and `debian-security`) when
  `apt-get` starts failing. Ports that ship bullseye's libssl 1.1 should take the last update
  (1.1.1w-0+deb11u8) from there, not the chroot's original package.
- **SDL2 headers in bullseye are old** (SDL2 2.0.14, SDL2_ttf 2.0.15) while the firmwares have new
  ones (Knulli: SDL2 2.30, SDL2_ttf 2.24). Link against the old ones and the firmware's library
  works. Guard newer API with `SDL_VERSION_ATLEAST`, or declare the few newer functions as weak
  symbols and call them only when they resolve.
- **The chroot is root-owned.** Copy the binary out with `sudo` and check the copy: a `cp` that
  failed inside a build pipeline left a stale binary in the zip.
- **A rootless sysroot** (no chroot, no root) can be made on any Debian or Ubuntu with
  `apt-get --download-only` and `dpkg-deb -x`, turning absolute symlinks into relative ones. Enough
  for the clang cross build.

### Cross building with clang

Use clang and lld with the chroot as sysroot, never Ubuntu's `aarch64-linux-gnu-gcc` with
`--sysroot`. The gcc cross toolchain searches its own glibc 2.39 headers and libraries before the
sysroot: a libmpv built that way needed GLIBC_2.38 (`__isoc23_strtol`, pthread symbols from 2.34).
The same build with

```bash
clang --target=aarch64-linux-gnu --sysroot=<chroot> -fuse-ld=lld -march=armv8-a ...
```

needed GLIBC_2.29 at most. The toolchain file
[`aarch64-bullseye.cmake`](../build/native-aarch64/aarch64-bullseye.cmake) sets this up for CMake.
Traps met on the way:

- CMake with `CMAKE_SYSROOT` finds the chroot's aarch64 `ninja` and tries to run it: pin
  `CMAKE_MAKE_PROGRAM` to the host's.
- Meson's `sys_root` property prefixes every pkg-config path, your own install prefix included:
  leave it out.
- Absolute symlinks in the sysroot made lld pick static `libdl.a`: make them relative.
- C++20 code with modules support in CMake: set `CMAKE_CXX_SCAN_FOR_MODULES=OFF`.
- The sysroot has GCC 10's C++ headers. Code written for a newer standard library needs patches.
  OpenLoco (clang 18; GCC 10 itself cannot compile its `using enum`) needed an overlay of
  `bits/ptr_traits.h` without the LWG 3545 `static_assert`, which its sfl iterators tripped. Some
  libraries detect the gap themselves (libplacebo falls back from
  float `std::to_chars` and no longer needs libstdc++ at all).
- Rust: the same host clang/lld with the chroot as sysroot cross-builds Rust programs without
  Docker (Ruffle). Collect the licences of every linked crate.

### The C-only Debian 10 sysroot

For plain C (box64, small shims) a sysroot made from Debian 10's `libc6`, `libc6-dev` and
`linux-libc-dev` arm64 packages gives glibc 2.28. `--sysroot` alone is not enough with Ubuntu's
cross gcc, which still searches its own headers first. Flags that worked:

```bash
CFLAGS="-nostdinc -isystem $(aarch64-linux-gnu-gcc -print-file-name=include) \
  -isystem $SR/usr/include/aarch64-linux-gnu -isystem $SR/usr/include --sysroot=$SR"
LDFLAGS="-B$SR/usr/lib/aarch64-linux-gnu -L$SR/lib/aarch64-linux-gnu -L$SR/usr/lib/aarch64-linux-gnu"
```

This sysroot has no libstdc++, and the host's GCC 13 C++ headers would need a newer libstdc++ than
old firmwares have. Anything with C++ goes to the bullseye chroot.

### Pinning a symbol version

Building on bullseye can still pull a newer symbol version than the rest of the binary needs:
`pow@GLIBC_2.29` and `powf@GLIBC_2.27` are typical. Pin them to the old versions with
`.symver pow,pow@GLIBC_2.17` (and the same for `powf`) in a header force-included by the build.

## Never link core libraries statically

PortMaster-New's `AGENTS.md` forbids bundling or statically linking libc, libstdc++, libpthread,
librt, SDL2 and the GL drivers. A static build looks like the easy answer to the glibc problem; it
is not accepted, and several of the author's binaries had to be rebuilt:

- Hocus Pocus shipped a static build until 2026-09-28; the chroot build with libstdc++ and libgcc
  dynamic needs glibc 2.17.
- glxsdl (linked `-static-libstdc++ -static-libgcc` against the Debian 10 sysroot) and unity4shrink
  (fully `-static`) were rebuilt in the bullseye chroot with everything dynamic (2026-10-02):
  glxsdl 265 -> 72 KB, unity4shrink 925 -> 175 KB, output byte-identical to the static builds,
  same speed.
- 3D Movie Maker (3DMMEx, bullseye chroot) was rebuilt on 2026-09-30 with libstdc++ taken from the
  firmware; its staging step checks that the newest `GLIBCXX_` version stays at or below 3.4.26
  (GCC 9). Not yet re-tested on a device (Oct 2026).

Rule: anything with C++ is built in the chroot (or with clang against it), all core libraries
dynamic.

What may be linked statically: libraries the firmwares do not have, instead of shipping them as
`.so` files. Examples: libBulletML and GLU (Torus Trooper), ODE 0.5 built from the original tarball
in the chroot (Mu-cade: newer ODE versions change behaviour and API), D's Phobos
(`-static-libphobos` is fine).

Compiler drivers do not always do what the flags say. gdc ignores `-static-libstdc++` and drops a
bare `-lstdc++`; spell it out:

```bash
-Wl,--as-needed ... -Wl,-Bdynamic -lstdc++
```

and check the result with `readelf -d` (below).

## ARMv8.0 portability

PortMaster devices range from Cortex-A35 and A53 cores (RK3326, H700, the little cores of the S922X)
to A55 and newer. Code for a newer ARM level crashes with SIGILL on the older cores.

- **Build with `-march=armv8-a -mtune=cortex-a55`, never `-mcpu=cortex-a55`.** `-mcpu=cortex-a55`
  targets ARMv8.2-A, so the compiler may emit the LSE atomics (Large System Extensions, added in
  ARMv8.1) and other post-8.0 instructions, which A53 and A35 cores do not have: SIGILL there.
  `-mtune` keeps the scheduling benefit without them. Libraries that want LSE (box64) detect it at
  runtime.
- **Prebuilt binaries** (macOS arm64 games through machismo, closed libraries) may already contain
  ARMv8.1+ instructions. Scan them with [`scan_isa.sh`](../machismo/) before promising a port:
  Spaghetti Celesti had 17 LSE atomics, 4 EOR3 and 1 BCAX, each of which had to be emulated
  (see [macOS games](engines/macos-machismo.md)).

## Bugs that only appear on ARM

Code that has run on x86 for years can still break on aarch64:

- **`char` is unsigned on ARM.** Check code that stores negative values in `char` before blaming
  the compiler.
- **Uninitialised memory differs.** An uninitialised pointer member (Domino-Chain's draw target)
  was 0 on the PC and garbage on ARM.
- **clang traps on undefined behaviour that GCC lets through.** `printf("%s", std::string)` became a
  trap instruction (SIGTRAP at start) with clang while GCC builds worked, and `-Wno-everything` in the
  project hid the warning. Build every new port once with warnings on and grep for
  `non-pod-varargs` and `return-type`.
- **Old Windows or DOS code collides with POSIX names.** A global `unsigned char pause` fails with
  "redeclared as different kind of entity" once `<unistd.h>` comes in; rename it. A function named
  `link` clashes with POSIX `link()` ("too few arguments"). Also watch for `read`, `write`, `index`,
  `yield`, `y1`, `j0`.
- **Floating point types differ.** D's `real` and C's `long double` are 128-bit software arithmetic
  on aarch64: see [native ports: D programs](engines/native.md#d-programs).

## Checking a binary before shipping

Run these on every binary and library the port ships:

```bash
# newest glibc symbol version: this is the port's min_glibc (or below it)
aarch64-linux-gnu-objdump -T game.aarch64 | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
# newest libstdc++ symbol version: must be <= GLIBCXX_3.4.28 (GCC 10)
aarch64-linux-gnu-objdump -T game.aarch64 | grep -o 'GLIBCXX_[0-9.]*' | sort -uV | tail -1
# what it loads: no bundled SDL2/libc/libstdc++/GL, every other entry either on the CFWs or in libs.aarch64
aarch64-linux-gnu-readelf -d game.aarch64 | grep NEEDED
```

- The highest `GLIBC_` version over all shipped binaries goes into `port.json` as `min_glibc`
  (a runtime's own requirement counts too: `python_3.11` needs 2.30).
- A bundled library is named exactly as the `NEEDED` entry that asks for it.
- For prebuilt aarch64 code, also run the ISA scan above.
- Keep an unstripped copy of every shipped binary with the build: crash addresses from the device
  are resolved against it (`llvm-addr2line -f -C -e <unstripped binary> <offset>`), see
  [testing and debugging](testing-and-debugging.md).

## Build hygiene

- **Delete the old binary before building.** A failed link otherwise leaves the previous binary in
  place, and it gets shipped as if it were new.
- **Run the README's Compile section verbatim** in a clean directory and compare the result with
  the shipped binary byte for byte.
- **Prove that a cleanup changed nothing**: when rewriting or slimming a tool, run old and new on
  the same input and `cmp` the outputs, and run old and new aarch64 binaries under qemu in the
  chroot. A texture shrinker rewrite (606 -> 311 lines) was byte-identical in every setup mode, and
  the comparison caught a code path that looked unused. Then one clean install and play on the
  device.
- **Upstream moved to SDL3?** Many projects have a last SDL2 tag (Taisei before 1.4.3, Bugdom 2
  before its SDL3 switch); building that is usually simpler than bridging SDL3. More on source
  ports: [native engines](engines/native.md).
- **Generated data**: for games whose data is rendered at build time (POV-Ray in Domino-Chain and
  Toppler), use the release tarball's or Debian's packaged data instead of rendering it.
