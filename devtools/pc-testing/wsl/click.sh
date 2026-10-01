#!/bin/bash
# usage: click.sh x y [button]  -- click inside the game window (window coordinates)
id=$(xdotool search --name "^Gone Home$" | head -1)
[ -z "$id" ] && { echo "no window"; exit 1; }
xdotool windowactivate --sync "$id" 2>/dev/null
xdotool mousemove --window "$id" "$1" "$2"; sleep 0.2; xdotool mousedown "${3:-1}"; sleep 0.15; xdotool mouseup "${3:-1}"; echo "clicked $1,$2"
