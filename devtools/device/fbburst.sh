#!/bin/bash
# Burst of screenshots of the handheld's screen, taken ON the device so the frames are only
# <interval> apart (fbshot.sh needs seconds per frame over SSH): fbburst.sh <outdir> <count> <interval> ['<cmd run on the device first>']
# The optional command (e.g. 'echo a:1:0.5 > /tmp/pad.fifo') starts in the background right before
# the first frame. Frames go to /tmp on the device (RAM), are pulled in one tar and converted to
# <outdir>/f00.png ... in WSL with ImageMagick. 32 bpp framebuffers only (RG Cube XX). License: 0BSD.
set -e
OUT=$1; N=${2:-10}; IV=${3:-0.3}; PRE=$4
[ -n "$OUT" ] || { echo "usage: fbburst.sh <outdir> <count> <interval> [device cmd]"; exit 1; }
DEV="$(dirname "$0")/dev.sh"
read -r W STRIDE H <<< "$(sh "$DEV" 'cd /sys/class/graphics/fb0; echo $(cut -d, -f1 virtual_size) $(cat stride) $(sed -n "s/.*:[0-9]*x\([0-9]*\).*/\1/p" modes | head -1)' | tr -d '\r')"
mkdir -p "$OUT"
sh "$DEV" "rm -rf /tmp/fbb; mkdir /tmp/fbb; ( $PRE ) & i=0; while [ \$i -lt $N ]; do head -c $((STRIDE * H)) /dev/fb0 > /tmp/fbb/\$(printf %02d \$i).raw; i=\$((i+1)); sleep $IV; done; wait" >/dev/null
sh "$DEV" 'cd /tmp/fbb && tar cf - *.raw && rm -rf /tmp/fbb' > "$OUT/frames.tar"
WOUT=$(wsl.exe -e wslpath -a "$(cygpath -w "$(realpath "$OUT")")" | tr -d '\r')
wsl.exe -e bash -c "cd '$WOUT' && tar xf frames.tar && for f in *.raw; do convert -size $((STRIDE / 4))x$H -depth 8 bgra:\$f -alpha off -crop ${W}x$H+0+0 f\${f%.raw}.png && rm \$f; done; rm frames.tar"
ls "$OUT" | head -3; echo "$N frames in $OUT"
