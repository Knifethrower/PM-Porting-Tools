#!/bin/bash
# usage: build_device_test.sh <out file>
# Builds an aarch64 test binary from the working tree in the chroot for direct deployment to a
# device (Knulli shares the port folder over SMB as a guest share): with the timing log
# (fpslog_patch.py), the sampling profiler (prof.c) and unstripped, so that prof_report.py can
# name the functions. Not the release binary.
set -e
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
SYSROOT=${SYSROOT:-$HOME/tt_re/sysroot-arm64}
OUT=$1
T=$(mktemp -d)
cp -r "$PROJ/source/sources" "$T/sources"
python3 "$PROJ/pctest/fpslog_patch.py" "$T/sources"
cp "$PROJ/pctest/prof.c" "$T/sources/import/prof.c"
sed -i 's/^gdc -o Torus_Trooper -s /gdc -o Torus_Trooper /' "$T/sources/buildPortMaster.sh"
grep -q "^gdc -o Torus_Trooper -Wl" "$T/sources/buildPortMaster.sh"
sudo rm -rf "$SYSROOT/build/tt_dev"
sudo cp -r "$T/sources" "$SYSROOT/build/tt_dev"
rm -rf "$T"
sudo chroot "$SYSROOT" /bin/bash -e -c '
cd /build/tt_dev
rm -f Torus_Trooper
# the build script removes import/*.o first, so the profiler object is made under another name
gcc -O2 -c -o /build/tt_dev/prof_obj import/prof.c
sed -i "s|^gdc -o Torus_Trooper |gdc -o Torus_Trooper /build/tt_dev/prof_obj |" buildPortMaster.sh
sh ./buildPortMaster.sh 2>&1 | grep -v "cannot remove" || true
test -f Torus_Trooper
'
sudo cp "$SYSROOT/build/tt_dev/Torus_Trooper" "$OUT"
sudo chown "$(id -u):$(id -g)" "$OUT"
ls -l "$OUT"
