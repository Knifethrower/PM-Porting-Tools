#!/bin/bash
# Run a Godot 4 game's .pck on a stock Godot binary in a private Xvfb, logging output, RSS/peak RSS
# per second and screenshots at 20/40/60/90/120/150/180 s: what does the game need, how much RAM.
#   GODOT=<godot binary> PCK=<game.pck> run_pck.sh <outdir> <seconds> [extra godot args...]
# Matching official Godot binaries (same 4.x version as the game) are on godotengine.org; the
# export templates' linux_debug.x86_64 also prints script errors.
#   OVERRIDE=<file>  copied next to the binary as override.cfg with @DIR@ replaced by the run folder:
#                    add autoloads (stubs for missing GDExtensions, probe/MemProbe.gd) or features
#                    (probe/override.example.cfg)
#   STUBS=<dir>      copied to <run folder>/stubs (point the override's autoloads at @DIR@/stubs/...)
#   PROBE=1          copy probe/MemProbe.gd (+ ASSETS=<assets.tsv from pck_assets.py>) to <run folder>/probe
#   RES=WxH (640x480)  XDISP (77)  RUN (run folder, default /tmp/godot-run/<outdir name>)
# From Dome Keeper (runtime/run_test.sh).
OUT=$(realpath -m "$1"); secs=$2; shift 2
[ -x "$GODOT" ] && [ -f "$PCK" ] && [ -n "$secs" ] || { echo "usage: GODOT=<bin> PCK=<pck> run_pck.sh <outdir> <seconds> [godot args]"; exit 1; }
HERE=$(dirname "$(realpath "$0")")
D=${RUN:-/tmp/godot-run/$(basename "$OUT")}
X=${XDISP:-77}
rm -rf "$D"; mkdir -p "$D" "$OUT"
cp "$GODOT" "$D/godot"; chmod +x "$D/godot"
ln -sf "$(realpath "$PCK")" "$D/game.pck"
[ -n "$STUBS" ] && cp -r "$STUBS" "$D/stubs"
if [ "$PROBE" = 1 ]; then
  mkdir -p "$D/probe"; sed "s#@DIR@#$D#g" "$HERE/probe/MemProbe.gd" > "$D/probe/MemProbe.gd"
  [ -n "$ASSETS" ] && cp "$ASSETS" "$D/probe/assets.tsv"
fi
[ -n "$OVERRIDE" ] && sed "s#@DIR@#$D#g" "$OVERRIDE" > "$D/override.cfg"
cd "$D"
Xvfb :$X -screen 0 1280x720x24 -nolisten tcp >/dev/null 2>&1 &
xpid=$!
sleep 1
export DISPLAY=:$X GODOT_SILENCE_ROOT_WARNING=1
rm -f "$OUT"/shot-*.png
: > "$OUT/mem.txt"
"$D/godot" --main-pack "$D/game.pck" --audio-driver Dummy --rendering-driver opengl3 \
  --resolution ${RES:-640x480} "$@" > "$OUT/log.txt" 2>&1 &
pid=$!
for i in $(seq 1 "$secs"); do
  [ -d /proc/$pid ] || { echo "exited at ${i}s" >> "$OUT/mem.txt"; break; }
  echo "$i $(grep -E 'VmRSS|VmHWM' /proc/$pid/status | tr -s ' ' | tr '\n' ' ')" >> "$OUT/mem.txt"
  case $i in 20|40|60|90|120|150|180) import -window root "$OUT/shot-$i.png" 2>/dev/null;; esac
  sleep 1
done
cp "$D"/probe-*.tsv "$OUT/" 2>/dev/null
kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null
kill $xpid
echo "$OUT: $(tail -1 "$OUT/mem.txt")"
