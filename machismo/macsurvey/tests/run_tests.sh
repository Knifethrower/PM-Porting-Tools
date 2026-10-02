#!/bin/bash
# Build the fixtures, record LLVM's view of them, run the Python tests (WSL).
# Usage: run_tests.sh [WORKDIR]   (default /tmp/macsurvey-test)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
W=${1:-/tmp/macsurvey-test}
NM=$(command -v llvm-nm || command -v llvm-nm-18)
OTOOL=$(command -v llvm-otool || command -v llvm-otool-18)
bash "$HERE/make_fixtures.sh" "$W/fix"
mkdir -p "$W/nm"
for exe in SDLGame/SDLGame MetalGame/MetalGame OldGame/OldGame; do
    f="$W/fix/${exe%/*}.app/Contents/MacOS/${exe#*/}"
    for a in arm64 x86_64; do
        if "$OTOOL" -arch $a -L "$f" > "$W/nm/${exe#*/}.$a.otool" 2>/dev/null; then
            # --arch is refused for thin files: fall back to plain -u
            { "$NM" -u --arch=$a "$f" 2>/dev/null || "$NM" -u "$f"; } \
                | sed 's/^ *U //' > "$W/nm/${exe#*/}.$a.nm"
        else
            rm -f "$W/nm/${exe#*/}.$a.otool"
        fi
    done
done
python3 "$HERE/test_macsurvey.py" "$W/fix" "$W/nm"
