#!/bin/bash
# Piglit (Mesa's GL test suite, built with GLX in setup_piglit.sh) on a gl4es variant, then the reference on
# only the tests gl4es did not skip; piglit_diff.py lists what passes on Mesa and not through gl4es.
#   run_piglit.sh [variant] [profile] [jobs]      defaults: ship quick_gl 6
# Results: $GT/piglit-results/<variant>/ and .../ref-for-<variant>/, report piglit-<variant>.md
HERE=$(cd "$(dirname "$0")" && pwd); . "$HERE/env.sh"
V=${1:-ship}; PROFILE=${2:-quick_gl}; J=${3:-6}
P=$GT/piglit-src; R=$GT/piglit-results; mkdir -p "$R"
start_xvfb 95 || exit 1
trap 'kill $XVFB_PID' EXIT
export PIGLIT_PLATFORM=glx PIGLIT_BUILD_DIR=$P/build   # test binaries and generated profiles live in build/
# one test may hang: piglit's own per-test timeout
run() {   # run <name> <envvariant> [extra piglit args]
    local name=$1 v=$2; shift 2
    rm -rf "$R/$name"
    env $(gl4es_env $v) python3 "$P/piglit" run "$PROFILE" -c -j "$J" --timeout 60 "$@" "$R/$name" > "$R/$name.log" 2>&1
    python3 "$P/piglit" summary console -s "$R/$name" 2>/dev/null | tail -12
}
echo "== $V ($PROFILE)"
run "$V" "$V"
[ -d "$R/$V" ] || { echo "piglit run failed, see $R/$V.log"; exit 1; }
# tests gl4es ran (not skipped) -> the reference list
python3 - "$R/$V" > "$R/$V.ran.txt" <<'EOF'
import json, sys, os, bz2, gzip
d = sys.argv[1]
f = [x for x in os.listdir(d) if x.startswith('results.json')][0]
op = bz2.open if f.endswith('bz2') else gzip.open if f.endswith('gz') else open
r = json.load(op(os.path.join(d, f), 'rt'))
for name, t in r['tests'].items():
    if t['result'] != 'skip':
        print(name)
EOF
echo "== ref on $(wc -l < "$R/$V.ran.txt") tests"
run "ref-for-$V" ref --test-list "$R/$V.ran.txt"
python3 "$HERE/piglit_diff.py" "$R/ref-for-$V" "$R/$V" > "$R/piglit-$V.md"
head -40 "$R/piglit-$V.md"
