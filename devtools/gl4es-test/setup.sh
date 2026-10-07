#!/bin/bash
# Builds the gl4es variants the test bed compares (WSL, x86_64). Work dir: ~/gl4es-test (override with GT).
#   ship      a744af14 (Dec 28 2024 = Westonpack's "v1.1.7 built on Mar 10 2025") + the fixes the ports ship
#   ship-asan same, AddressSanitizer + UBSan (memory bugs that never show as wrong pixels, like the ARB-id one)
#   head      upstream master today + the same fixes (where they still apply)
#   orig      a744af14 with no fixes: must FAIL the regression tests (proves the bed sees known bugs)
#   fix       ship + patches/candidate-fixes.patch (fixes for FINDINGS.md, made by make_candidate.py); fix-asan
# All carry patches/mali-profile.patch. Usage: setup.sh [variant...]   (default: all)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
GT=${GT:-$HOME/gl4es-test}
SHIP_COMMIT=a744af14
mkdir -p "$GT"
[ -d "$GT/gl4es.git" ] || git clone -q --bare https://github.com/ptitSeb/gl4es.git "$GT/gl4es.git"
git -C "$GT/gl4es.git" fetch -q origin '+refs/heads/*:refs/heads/*' || true

build() {   # name commit fixes(0/1/2: 2 = also patches/candidate-fixes.patch) cflags
    local name=$1 commit=$2 fixes=$3 cflags=$4 src="$GT/src-$1"
    rm -rf "$src"; git -C "$GT/gl4es.git" worktree prune
    git -C "$GT/gl4es.git" worktree add -f -q --detach "$src" "$commit"
    ( cd "$src"
      patch -s -p1 < "$HERE/patches/mali-profile.patch"
      if [ "$fixes" = 1 ]; then
          # a fix upstream already made is skipped (-N), so head only gets what it still lacks
          patch -s -p1 -N -r - < "$HERE/patches/ship-fixes.patch" || true
      fi
      [ "$fixes" = 2 ] && { patch -s -p1 -N -r - < "$HERE/patches/ship-fixes.patch"; patch -s -p1 < "$HERE/patches/candidate-fixes.patch"; }
      mkdir -p build && cd build
      cmake .. -DNOX11=OFF -DNOEGL=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_FLAGS="$cflags" > build.log
      make -j"$(nproc)" >> build.log 2>&1 || { tail -20 build.log; exit 1; } )
    mkdir -p "$GT/lib/$name"; cp "$src/lib/libGL.so.1" "$GT/lib/$name/"
    echo "$name: $(git -C "$src" log -1 --format='%h %cd' --date=short) fixes=$fixes -> $GT/lib/$name/libGL.so.1"
}

V=${*:-ship ship-asan head orig}
for v in $V; do case $v in
    ship)      build ship      $SHIP_COMMIT 1 "" ;;
    ship-asan) build ship-asan $SHIP_COMMIT 1 "-fsanitize=address,undefined -fno-omit-frame-pointer" ;;
    head)      build head      master       1 "" ;;
    orig)      build orig      $SHIP_COMMIT 0 "" ;;
    fix)       build fix       $SHIP_COMMIT 2 "" ;;
    fix-asan)  build fix-asan  $SHIP_COMMIT 2 "-fsanitize=address,undefined -fno-omit-frame-pointer" ;;
    *) echo "unknown variant $v"; exit 1 ;;
esac; done
