#!/bin/bash
# usage: contact_sheet.sh <frames dir> [out.jpg]
# All f<N>.ppm/.png frame dumps of a folder (replay_compare.sh output) on one labelled sheet,
# 6 per row, in frame order: check a whole scripted run at a glance. Needs ImageMagick (montage).
# Default output: <frames dir>_sheet.jpg. From Bugdom 2 (pctest/sheet.sh).
D=$(realpath "$1")
OUT=${2:-${D}_sheet.jpg}
cd "$D" || { echo "usage: contact_sheet.sh <frames dir> [out.jpg]"; exit 1; }
F=$(ls f*.ppm f*.png 2>/dev/null | sort -V)
[ -n "$F" ] || { echo "no f*.ppm / f*.png in $D"; exit 1; }
montage -label '%t' $F -tile 6x -geometry 320x240+2+2 -pointsize 14 -quality 85 "$OUT" && echo "$OUT"
