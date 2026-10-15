#!/bin/bash
# usage: xvfb_click.sh x y [hold_seconds]  -- click in the Usagi window on the Xvfb display :99
export DISPLAY=:99
id=$(xdotool search --name "^Usagi" | tail -1)
xdotool mousemove --window "$id" "$1" "$2"; sleep 0.3
xdotool mousedown 1; sleep "${3:-0.3}"; xdotool mouseup 1
