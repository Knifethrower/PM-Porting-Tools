#!/bin/bash
# usage: verify.sh original.ogg repacked.ogg
# Decodes both files with ffmpeg's own Vorbis decoder and with libvorbis to 32-bit float and
# compares the samples bit for bit. Exit status 0 only if both decoders give identical output.
set -e
O=$1; N=$2
T=$(mktemp -d)
status=0
for dec in vorbis libvorbis; do
  ffmpeg -v error -y -c:a $dec -i "$O" -f f32le "$T/o.f32"
  ffmpeg -v error -y -c:a $dec -i "$N" -f f32le "$T/n.f32"
  if [ -s "$T/o.f32" ] && cmp -s "$T/o.f32" "$T/n.f32"; then
    echo "  $dec: identical, $(( $(stat -c%s "$T/n.f32") / 4 )) samples"
  else
    echo "  $dec: DIFFERENT ($(stat -c%s "$T/o.f32") vs $(stat -c%s "$T/n.f32") bytes)"
    status=1
  fi
done
rm -rf "$T"
exit $status
