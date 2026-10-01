#!/bin/bash
# usage: xvfb_shot.sh out.png  -- grab the whole 640x480 Xvfb screen (the game runs fullscreen)
export DISPLAY=:99
xwd -root -silent | convert xwd:- "$1"
