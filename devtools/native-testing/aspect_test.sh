#!/bin/bash
# usage: aspect_test.sh <binary> <shots dir> [WxH ...]
# Renders the game at every handheld screen size (replay_compare.sh with RES) and puts each size's
# frames into one contact sheet <shots dir>/aspect_<W>x<H>.png, to check Hor+/Vert+ and HUD
# anchoring. Default sizes: 640x480 720x720 480x320 1280x720 854x480 960x544
# 800x480 1024x768. Needs ImageMagick (montage).
#   FRAMES  frames per size (default "60 250 600 1000"); KEYS  scripted input (default: Z at 60)
#   WORK    where the raw dumps go (default /tmp/aspect); other replay_compare.sh variables pass through
# From Mu-cade (pctest/).
BIN=$(realpath "$1"); S=$(realpath -m "$2"); shift 2
[ -x "$BIN" ] && [ -n "$S" ] || { echo "usage: aspect_test.sh <binary> <shots dir> [WxH ...]"; exit 1; }
SIZES=${*:-"640x480 720x720 480x320 1280x720 854x480 960x544 800x480 1024x768"}
export FRAMES=${FRAMES:-"60 250 600 1000"}
W=${WORK:-/tmp/aspect}
P="$(dirname "$(realpath "$0")")"
mkdir -p "$S"
for r in $SIZES; do
  RES=$r TIMEOUT=${TIMEOUT:-120} bash "$P/replay_compare.sh" "$BIN" "$W/$r" | tail -1
  montage -label "$r" $(for f in $FRAMES; do echo "$W/$r/f$f.ppm"; done) \
    -tile 2x -geometry 'x360>+3+3' -background gray30 "$S/aspect_$r.png"
done
