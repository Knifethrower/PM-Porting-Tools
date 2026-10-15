#!/bin/bash
# Run a command inside the Debian bullseye arm64 build chroot (qemu-user binfmt), as root, with a
# project folder bind-mounted at /work. Binaries built there need only glibc 2.31 and GCC 10's
# libstdc++, which every current PortMaster CFW has.
#   wsl -u root -e bash chroot.sh <project dir> "<command>"
#   e.g. wsl -u root -e bash chroot.sh /mnt/c/src/mygame "bash /work/tools-src/build_3dmm.sh"
# CHROOT overrides the chroot (default: ~<first user>/chroot-bullseye). Setting it up: README.md.
# From 3D Movie Maker (tools-src/chroot.sh).
PROJ=$1; shift
[ -d "$PROJ" ] && [ -n "$*" ] || { echo "usage: chroot.sh <project dir> \"<command>\""; exit 1; }
R=${CHROOT:-/home/$(ls /home | head -1)/chroot-bullseye}
for m in proc sys dev dev/pts; do
    mountpoint -q "$R/$m" || mount --bind "/$m" "$R/$m"
done
mkdir -p "$R/work"
mountpoint -q "$R/work" && umount "$R/work"
mount --bind "$PROJ" "$R/work"
cp /etc/resolv.conf "$R/etc/resolv.conf"
exec chroot "$R" /usr/bin/env -i HOME=/root PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin LANG=C.UTF-8 /bin/bash -c "$*"
