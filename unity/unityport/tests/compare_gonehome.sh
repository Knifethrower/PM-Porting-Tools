#!/bin/bash
# Equivalence test: the Gone Home port's own install.py and `unityport install gonehome.toml`
# on two fresh copies of the GOG installer must produce identical game files.
# usage: compare_gonehome.sh <work dir>      (needs ~8 GB; Git Bash on Windows, Python with UnityPy)
set -e
W=$1
GH=${GH:?set GH=<folder with gone_home_*.sh and the port release/gonehome/tools/install.py>}
UP="$(cd "$(dirname "$0")/.." && pwd)"
export AUDIO_ADPCM=1
if [ "$KEEP_OLD" != 1 ]; then              # KEEP_OLD=1: reuse the old installer's output
  rm -rf "$W/old"; mkdir -p "$W/old"
  cp "$GH/gone_home_2020_01_28_35744.sh" "$W/old/"
  echo "== old installer"
  ( time python "$GH/release/gonehome/tools/install.py" "$W/old" ) > "$W/old.log" 2>&1
  tail -3 "$W/old.log"
fi
rm -rf "$W/new"; mkdir -p "$W/new"
cp "$GH/gone_home_2020_01_28_35744.sh" "$W/new/"
echo "== unityport"
( time PYTHONPATH="$UP" python -m unityport install "$UP/examples/gonehome.toml" "$W/new" ) > "$W/new.log" 2>&1
tail -3 "$W/new.log"
echo "== compare (state files excluded)"
cd "$W"
diff -rq old new -x .gh_install.json && echo "IDENTICAL"
