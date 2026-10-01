#!/bin/bash
# usage: shot.sh <out.png>  -- grabs the game window straight from X11
id=$(xdotool search --name "^Gone Home$" | head -1)
[ -z "$id" ] && { echo "no window"; exit 1; }
xwd -silent -id "$id" | convert xwd:- "$1" && echo "shot $1"
