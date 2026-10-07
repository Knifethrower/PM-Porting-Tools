#!/bin/bash
# Equivalence test: the Night in the Woods port's install.py (from the released port) and
# `unityport install nitw.toml` on two fresh copies of the GOG installer.
# usage: compare_nitw.sh <work dir>      (needs ~10 GB; Git Bash on Windows, Python with UnityPy)
set -e
W=$1
SH=${SH:?set SH=<the GOG installer night_in_the_woods_en_406_21109.sh>}
OLD=${OLD:?set OLD=<the NITW port nightinthewoods/tools/install.py>}
UP="$(cd "$(dirname "$0")/.." && pwd)"
if [ "$KEEP_OLD" != 1 ]; then              # KEEP_OLD=1: reuse the old installer's output
  rm -rf "$W/old"; mkdir -p "$W/old"
  cp "$SH" "$W/old/"
  echo "== old installer"
  ( time python "$OLD" "$W/old" ) > "$W/old.log" 2>&1
  tail -3 "$W/old.log"
fi
rm -rf "$W/new"; mkdir -p "$W/new"
cp "$SH" "$W/new/"
echo "== unityport"
( time PYTHONPATH="$UP" python -m unityport install "$UP/examples/nitw.toml" "$W/new" ) > "$W/new.log" 2>&1
tail -3 "$W/new.log"
echo "== compare (state files excluded)"
cd "$W"
diff -rq old new -x .nitw_install.json && echo "IDENTICAL"
