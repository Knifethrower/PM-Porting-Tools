#!/bin/bash
# EXAMPLE from a finished port (paths, names and DEVICE_IP are that project's): copy and adapt.
# PC side of the Cube driving session (Git Bash): cube_do.sh <cube_drive.sh command...>
# "shot <name>" saves pctest/cube_perf/drive_<name>.png (framebuffer page converted in WSL).
# "step <name> <spec>..." = keys, then shot + fps: one round trip per route step.
# Arguments are shell-quoted for the remote side, so patterns with spaces survive.
export MSYS_NO_PATHCONV=1
D="${OUT_DIR:-pctest/cube_perf}"; mkdir -p "$D"
WD=$(wsl.exe -e wslpath -a "$(cygpath -w "$(realpath "$D")")" | tr -d "\r")
C="wsl.exe -e ssh -S /tmp/device.sock -o BatchMode=yes root@DEVICE_IP"
remote() { local q; q=$(printf '%q ' "$@"); $C "UINPUT_SETTLE=${UINPUT_SETTLE:-2} DRIVE_ENV=$(printf '%q' "$DRIVE_ENV") DRIVE_LAUNCHER=$(printf '%q' "$DRIVE_LAUNCHER") bash /tmp/cube_drive.sh $q"; }
shot() {
  $C "bash /tmp/cube_drive.sh shot" > "$D/drive_$1.raw" && \
  wsl.exe -e bash -c "cd '$WD' && convert -size 720x720 -depth 8 bgra:drive_$1.raw drive_$1.png && rm drive_$1.raw" && echo "shot drive_$1.png"
}
case "$1" in
  shot) shot "$2";;
  step) n=$2; shift 2; remote keys "$@"; shot "$n"; remote fps 2;;
  *) remote "$@";;
esac
