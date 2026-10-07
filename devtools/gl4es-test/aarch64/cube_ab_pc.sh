#!/bin/bash
# PC side (Git Bash) of the gl4es A/B test on the RG Cube XX, through the user's shared SSH socket. DEV_HOST / DEV_SOCK as
# for devtools/device/dev.sh. Ask first if someone else uses the device.
#   cube_ab_pc.sh push                        cube_ab.sh, both gl4es builds (/tmp/gl4es-<ship|fix>.so), fpslog, uinput
#   cube_ab_pc.sh install <game> <orig|ship|fix>
#   cube_ab_pc.sh run <game> <tag> <seconds> [keys...]   -> results/cube/<tag>.txt + .png
#   cube_ab_pc.sh restore <game> | es
export MSYS_NO_PATHCONV=1
HERE="$(cd "$(dirname "$0")" && pwd)"; TOP="$HERE/../.."
C="wsl.exe -e ssh -S ${DEV_SOCK:-/tmp/device.sock} -o BatchMode=yes ${DEV_HOST:?set DEV_HOST=root@<device ip>}"
OUT="$HERE/../results/cube"; mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
wslp() { wsl.exe -e wslpath -a "$(cygpath -w "$1")" | tr -d "\r"; }
put() { $C "cat > $2.new && mv -f $2.new $2" < "$1"; }   # rename: a running process keeps its mapped copy
case "$1" in
  push)
    put "$HERE/cube_ab.sh" /tmp/cube_ab.sh
    for v in ship fix; do
        wsl.exe -e bash -c "cat ~/gl4es-test/aarch64/$v/libGL.so.1" | $C "cat > /tmp/gl4es-$v.so.new && mv -f /tmp/gl4es-$v.so.new /tmp/gl4es-$v.so"
    done
    put "$TOP/device/uinput_keys.py" /tmp/cube_uinput.py
    wsl.exe -e bash -c "cd '$(wslp "$TOP/profiling/fpslog")' && gcc -O2 -shared -fPIC -o /tmp/libfpslog.so fpslog.c -ldl && cat /tmp/libfpslog.so" \
        | $C "cat > /tmp/libfpslog.so.new && mv -f /tmp/libfpslog.so.new /tmp/libfpslog.so"
    $C "sha1sum /tmp/gl4es-*.so | cut -c1-8,41-; ls -l /tmp/cube_ab.sh /tmp/libfpslog.so /tmp/cube_uinput.py | awk '{print \$5, \$9}'";;
  install)
    case "$3" in
        orig) $C "bash /tmp/cube_ab.sh restore $2";;
        *)    $C "bash /tmp/cube_ab.sh install $2 /tmp/gl4es-$3.so $3";;
    esac;;
  run)
    G=$2; T=$3; shift 3
    $C "bash /tmp/cube_ab.sh run $G $T $*" | tee "$OUT/$T.txt"
    $C "cat /tmp/ab_$T.raw" > "$OUT/$T.raw" && \
    wsl.exe -e bash -c "cd '$(wslp "$OUT")' && convert -size 720x720 -depth 8 bgra:$T.raw -alpha off $T.png && rm $T.raw" && echo "shot $T.png";;
  restore) $C "bash /tmp/cube_ab.sh restore $2";;
  es) $C "bash /tmp/cube_ab.sh es";;
  *) sed -n 2,7p "$0";;
esac
