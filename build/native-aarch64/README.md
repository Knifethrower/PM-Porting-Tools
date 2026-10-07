# Native aarch64 build environment

Builds native ports (games with source) so they run on every current PortMaster CFW: link against
Debian bullseye (glibc 2.31, libstdc++ from GCC 10), never against the build host's newer glibc.
Used by 3D Movie Maker, OpenLoco, Hocus Pocus, Cube, Mu-cade, Torus Trooper and Stunt Playground.

## The chroot (once, in WSL)

```bash
sudo apt-get install debootstrap qemu-user-static binfmt-support rsync zip
sudo debootstrap --arch=arm64 bullseye ~/chroot-bullseye http://deb.debian.org/debian
sudo chroot ~/chroot-bullseye apt-get install -y build-essential cmake ninja-build pkg-config \
     libsdl2-dev libsdl2-mixer-dev libsdl2-image-dev libsdl2-ttf-dev
```

Add whatever `-dev` packages the game needs. Mesa in the chroot also gives a software GLES 2/3
for PC tests under qemu (`../../devtools/native-testing/pm_sim_test.sh`, `arm_replay_compare.sh`).

## Two ways to build

| | How | When |
|--|--|--|
| Inside the chroot | `chroot.sh <project> "<command>"` (as root: `wsl -u root`) runs GCC 10 under qemu | small projects, autotools, anything that wants to run its own build tools; slow (emulated compiler) |
| Cross from the host | `cmake -DCMAKE_TOOLCHAIN_FILE=aarch64-bullseye.cmake` (host clang + lld, chroot as sysroot) | big C++ projects (OpenLoco); native speed, needs `clang` and `lld` in WSL. Code that needs a newer C++ standard library than GCC 10 needs patches (OpenLoco's `libstdcxx10.patch`) |

Check what a binary needs before shipping:

```bash
aarch64-linux-gnu-objdump -T game.aarch64 | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1   # <= 2.31
aarch64-linux-gnu-readelf -d game.aarch64 | grep NEEDED
```

## Files

- `chroot.sh`: run a command in the chroot with a project folder at `/work`.
- `aarch64-bullseye.cmake`: CMake toolchain file for the cross build.
- `collect_licenses.example.sh`: 3DMM's script that copies one `LICENSE.<component>.txt` per shipped
  component from the upstream sources (PortMaster wants one file per bundled component).

Other routes seen: Micropolis used a Bootlin glibc 2.35 sysroot with Ubuntu's GCC 13 (works on
fewer firmwares); box64 and the Unity shims use a glibc 2.28 Debian 10 sysroot (`~/sysroot-buster`).

## Bullseye is archived (2026-10)

Debian 11 left LTS in September 2026: its packages and security updates moved to `archive.debian.org`, and
`deb.debian.org` returns 404 for files its indexes still list. `~/chroot-bullseye` now also has
`deb http://archive.debian.org/debian-security bullseye-security main` (sources.list.d); point the other
lines at `http://archive.debian.org/debian` too if `apt-get install` in the chroot starts failing. Ports
that ship bullseye's libssl 1.1: take it from there (1.1.1w-0+deb11u8, not the chroot's old deb11u1).
A rootless way to make such a sysroot on any Debian/Ubuntu (apt --download-only + dpkg-deb -x, absolute
symlinks made relative): `C:\Claude\Jellyfin MPV Shim\public\setup.sh`.
