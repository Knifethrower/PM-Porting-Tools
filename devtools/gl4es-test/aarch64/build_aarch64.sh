#!/bin/bash
# Builds the ports' gl4es for aarch64 from source, as a drop-in for Westonpack's gl4es_glxpass libGL.so.1:
# a744af14 (= Westonpack's v1.1.7 base) + the ports' fixes (ARB ids, u16 read, Teslagrad's main FBO) + patches,
# NOX11 + NOEGL + NO_INIT_CONSTRUCTOR like the shipped build (glxsdl calls initialize_gl4es on its SDL context),
# glX* forwarded to crusty_glX* (glxpass.c; the ports' glxsdl provides those), GCC 10 in the Debian bullseye arm64
# chroot (glibc 2.31 like every CFW). Exports match the shipped binary (checked with nm against it).
#   build_aarch64.sh [ship|fix ...]     ship = the ports' fixes only, fix = + candidate-fixes.patch
# Out: $GT/aarch64/<variant>/libGL.so.1 (debug info stripped, symbols kept) + libGL.so.1.debug. Needs root for the chroot:
#   wsl -u root -e bash build_aarch64.sh fix      (sources are prepared as the normal user first)
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
U=$(ls /home | head -1); UH=/home/$U
GT=${GT:-$UH/gl4es-test}
C=${CHROOT:-$UH/chroot-bullseye}
for V in ${*:-ship fix}; do
    S=$GT/src-a64-$V; rm -rf "$S"   # as root: a failed chroot build leaves root-owned files
    sudo -u "$U" bash "$HERE/aarch64/prep_a64.sh" "$S" "$V" "$HERE" "$GT/gl4es.git"
    bash "$HERE/../../build/native-aarch64/chroot.sh" "$S" "cd /work && rm -rf build && mkdir build && cd build && \
        cmake .. -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DNOX11=ON -DNOEGL=ON -DNO_INIT_CONSTRUCTOR=ON -DGLX_STUBS=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo > cmake.log && make -j\$(nproc) > make.log 2>&1 \
        || { tail -30 make.log; exit 1; }"
    O=$GT/aarch64/$V; mkdir -p "$O"
    cp "$S/lib/libGL.so.1" "$O/libGL.so.1.debug"
    # keep .symtab: the ports' glxsdl finds createMainFBO / blitMainFBO / ... by name there (Teslagrad letterbox)
    aarch64-linux-gnu-strip --strip-debug -o "$O/libGL.so.1" "$S/lib/libGL.so.1"
    chown -R "$U" "$O" "$S"
    echo "$V: $(sha1sum "$O/libGL.so.1" | cut -c1-8) $(stat -c %s "$O/libGL.so.1") bytes, glibc max $(aarch64-linux-gnu-objdump -T "$O/libGL.so.1" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
done
