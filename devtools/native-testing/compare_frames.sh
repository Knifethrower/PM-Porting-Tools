#!/bin/bash
# usage: compare_frames.sh <dir A> <dir B> <out dir>   (needs ImageMagick; from Torus Trooper)
# Compares every f<N>.ppm present in both directories (replay_compare.sh dumps) and prints the
# number of differing pixels per frame; a frame that differs gets <out dir>/diff<N>.png (A, the
# ImageMagick diff, B side by side) for inspection.
A=$1; B=$2; OUT=$3
mkdir -p "$OUT"
total=0
for fa in $(ls "$A"/f*.ppm | sed 's/.*\/f//; s/\.ppm//' | sort -n); do
  fb="$B/f$fa.ppm"
  [ -f "$fb" ] || { echo "f$fa: only in $A"; continue; }
  n=$(compare -metric AE "$A/f$fa.ppm" "$fb" "$OUT/d.png" 2>&1)
  if [ "$n" = 0 ]; then
    echo "f$fa: identical"
  else
    echo "f$fa: $n pixels differ"
    convert "$A/f$fa.ppm" "$OUT/d.png" "$fb" +append "$OUT/diff$fa.png"
    total=$((total + 1))
  fi
done
rm -f "$OUT/d.png"
echo "$total frames differ"
