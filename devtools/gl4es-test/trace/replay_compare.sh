#!/bin/bash
# Replays a trace on the reference (Mesa desktop GL) and on a gl4es variant, saves every Nth frame through
# snapshim.c and compares them (replay_compare.py): the input is identical, differences come from gl4es.
#   replay_compare.sh <trace> [variant] [every N frames]      defaults: ship 10
HERE=$(cd "$(dirname "$0")/.." && pwd); . "$HERE/env.sh"
T=$1; V=${2:-ship}; N=${3:-10}
NAME=$(basename "$T" .trace); OUT=$GT/replays/$NAME; mkdir -p "$OUT"
shim() {   # shim <variant>: directory with the snapshot libGL.so.1 in front of that variant's real libGL
    local v=$1 lib=${1%%@*} d=$GT/snapshim/${1%%@*}
    mkdir -p "$d/stub"
    # link against an empty stub with soname libsnapreal.so (the real library's soname is libGL.so.1 = the shim)
    echo 'void snapreal_stub(void) {}' > "$d/stub/s.c"
    gcc -shared -fPIC -o "$d/stub/libsnapreal.so" "$d/stub/s.c" -Wl,-soname,libsnapreal.so
    gcc -O1 -shared -fPIC -o "$d/libGL.so.1" "$HERE/trace/snapshim.c" -Wl,-soname,libGL.so.1 -L"$d/stub" -Wl,--no-as-needed -lsnapreal -Wl,--as-needed \
        -Wl,-rpath,'$ORIGIN' -lX11 -ldl
    if [ "$lib" = ref ]; then ln -sf /lib/x86_64-linux-gnu/libGL.so.1 "$d/libsnapreal.so"
    else ln -sf "$GT/lib/$lib/libGL.so.1" "$d/libsnapreal.so"; fi
    echo "$d"
}
start_xvfb 97 || exit 1
for v in ref $V; do
    rm -rf "$OUT/$v"; mkdir -p "$OUT/$v"
    D=$(shim $v)
    env $(gl4es_env $v) LD_LIBRARY_PATH="$D" SNAP_DIR="$OUT/$v" SNAP_EVERY=$N timeout 3600 glretrace "$T" > "$OUT/$v.log" 2>&1
    echo "$v: $(ls "$OUT/$v" | wc -l) snapshots, exit $?"
done
kill $XVFB_PID
python3 "$HERE/trace/replay_compare.py" "$OUT/ref" "$OUT/$V" "$OUT/diff-$V" | tee "$OUT/report-$V.md" | tail -40
