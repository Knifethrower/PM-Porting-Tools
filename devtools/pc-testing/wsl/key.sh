#!/bin/bash
# usage: key.sh <xdotool key names...>  -- focus the game window and send keys (held 120ms each)
id=$(xdotool search --name "^Gone Home$" | head -1)
[ -z "$id" ] && { echo "no window"; exit 1; }
xdotool windowactivate --sync "$id" 2>/dev/null; xdotool windowfocus --sync "$id" 2>/dev/null
for k in "$@"; do xdotool keydown "$k"; sleep 0.12; xdotool keyup "$k"; sleep 0.3; done
echo "sent $*"
