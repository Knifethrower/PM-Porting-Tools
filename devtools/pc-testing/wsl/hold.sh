#!/bin/bash
# usage: hold.sh key seconds  -- hold a key down
id=$(xdotool search --name "^Gone Home$" | head -1)
xdotool windowactivate --sync "$id" 2>/dev/null
xdotool keydown "$1"; sleep "$2"; xdotool keyup "$1"; echo "held $1 $2s"
