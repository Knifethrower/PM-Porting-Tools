#!/bin/bash
# Screenshot of the handheld's screen from the PC (Git Bash), read straight from /dev/fb0 through
# dev.sh, converted with ImageMagick in WSL: fbshot.sh <out.png>
# Size, depth and stride come from /sys/class/graphics/fb0; the first page is taken (on the
# RG Cube XX that is the page on screen). Works on KMS/DRM firmwares that keep the fbdev emulation;
# on Wayland/Weston (ROCKNIX) use the compositor's own screenshot tool instead.
# FB_FORMAT overrides the pixel layout (default bgra for 32 bpp, rgb565 for 16 bpp).
# From Ittle Dew (pctest/cube_do.sh shot).
set -e
OUT=$1; [ -n "$OUT" ] || { echo "usage: fbshot.sh <out.png>"; exit 1; }
DEV="$(dirname "$0")/dev.sh"
read -r W BPP STRIDE <<< "$(sh "$DEV" 'cd /sys/class/graphics/fb0; echo $(cut -d, -f1 virtual_size) $(cat bits_per_pixel) $(cat stride 2>/dev/null)' | tr -d '\r')"
# virtual_size is "W,Hvirtual" (all pages); the visible height is in modes ("U:WxHp-60")
H=$(sh "$DEV" 'sed -n "s/.*:[0-9]*x\([0-9]*\).*/\1/p" /sys/class/graphics/fb0/modes | head -1')
STRIDE=${STRIDE:-$((W * BPP / 8))}
FMT=${FB_FORMAT:-$([ "$BPP" = 16 ] && echo rgb565 || echo bgra)}
RAW=$(mktemp)
sh "$DEV" "head -c $((STRIDE * H)) /dev/fb0" > "$RAW"
WRAW=$(wsl.exe -e wslpath -a "$(cygpath -w "$RAW")" | tr -d '\r')
WOUT=$(wsl.exe -e wslpath -a "$(cygpath -w "$(realpath -m "$OUT")")" | tr -d '\r')
SW=$((STRIDE * 8 / BPP))
if [ "$FMT" = rgb565 ]; then
  wsl.exe -e bash -c "ffmpeg -v error -y -f rawvideo -pix_fmt rgb565le -s ${SW}x$H -i '$WRAW' -vf crop=$W:$H:0:0 '$WOUT'"
else
  wsl.exe -e bash -c "convert -size ${SW}x$H -depth 8 $FMT:'$WRAW' -alpha off -crop ${W}x$H+0+0 '$WOUT'"
fi
rm -f "$RAW"
echo "$OUT (${W}x$H, $BPP bpp)"
