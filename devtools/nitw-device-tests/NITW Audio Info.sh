#!/bin/bash
# Collects the device's ALSA/audio setup into ports/nightinthewoods/audioinfo.txt
# (run from the Ports menu or over SSH). Plays only silence.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then controlfolder="$XDG_DATA_HOME/PortMaster"
else controlfolder="/roms/ports/PortMaster"; fi
[ -f "$controlfolder/control.txt" ] && source "$controlfolder/control.txt" 2>/dev/null

GAMEDIR="/${directory:-roms}/ports/nightinthewoods"
[ -d "$GAMEDIR" ] || GAMEDIR="$(cd "$(dirname "$0")" && pwd)/nightinthewoods"
OUT="$GAMEDIR/audioinfo.txt"

section() { echo; echo "==================== $* ===================="; }
show() { section "$1"; shift; "$@" 2>&1; }
showfile() { section "file: $1"; if [ -e "$1" ]; then ls -la "$1"; cat "$1" 2>&1; else echo "(not present)"; fi; }

{
  echo "NITW audio info - $(date)"
  echo "user: $(id)"; echo "CFW: ${CFW_NAME:-?} ${CFW_VERSION:-}  device: ${DEVICE_NAME:-?}"
  uname -a

  showfile /etc/asound.conf
  for f in /etc/alsa/conf.d/*; do [ -e "$f" ] && showfile "$f"; done
  showfile /home/ark/.asoundrc
  showfile /root/.asoundrc
  showfile "$HOME/.asoundrc"
  showfile /usr/share/alsa/alsa.conf.d
  showfile /proc/asound/cards
  showfile /proc/asound/pcm

  show "aplay -l (hardware devices)" aplay -l
  show "aplay -L (all PCM names)" aplay -L
  show "amixer controls (card 0)" amixer -c 0 scontrols
  show "amixer contents (card 0, first 80 lines)" sh -c 'amixer -c 0 contents | head -80'

  section "sound servers running"
  ps aux 2>/dev/null | grep -Ei "pulse|pipewire|wireplumber|jack|bluealsa|alsa" | grep -v grep || echo "(none found)"

  section "open handles on sound devices"
  (command -v fuser >/dev/null && fuser -v /dev/snd/* 2>&1) || ls -la /dev/snd/

  # What block (period) size does each device really give when asked for 1024 frames?
  # FMOD asks for 1024 @ 48 kHz stereo. Plays 1 second of silence per device.
  for dev in default ddmix softvol "plughw:CARD=rockchiprk817co,DEV=0" "hw:CARD=rockchiprk817co,DEV=0"; do
    section "test open: $dev  (period 1024, buffer 4096, 48000 Hz, S16_LE stereo)"
    timeout 5 aplay -v -D "$dev" -f S16_LE -r 48000 -c 2 --period-size=1024 --buffer-size=4096 \
      -d 1 /dev/zero 2>&1 | grep -Ei "period_size|buffer_size|rate|format|channels|error|busy|slave|Plug|Hooks|Direct|Soft|:" | head -40
    echo "exit code: ${PIPESTATUS[0]}"
  done

  section "hw_params currently in use (if anything is playing)"
  for f in /proc/asound/card*/pcm*p/sub*/hw_params; do echo "$f:"; cat "$f"; done 2>/dev/null
} > "$OUT" 2>&1

echo "Wrote $OUT"
command -v pm_message >/dev/null 2>&1 && pm_message "Audio info saved to ports/nightinthewoods/audioinfo.txt" && sleep 3
command -v pm_finish >/dev/null 2>&1 && pm_finish
exit 0
