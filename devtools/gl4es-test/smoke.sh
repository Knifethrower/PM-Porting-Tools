#!/bin/bash
# Quick check that a variant loads and piglit tests can talk to it: smoke.sh [variant]
HERE=$(cd "$(dirname "$0")" && pwd); . "$HERE/env.sh"
V=${1:-ship}; P=/usr/lib/x86_64-linux-gnu/piglit/bin
start_xvfb 91 || exit 1
for v in ref $V; do
    echo "== $v"
    env $(gl4es_env $v) glxinfo -B 2>&1 | grep -E "OpenGL (renderer|version)|direct rendering"
    for t in "texture-packed-formats" "fbo-1d -auto" "glsl-fs-mix -auto" "arbfp-tex -auto"; do
        set -- $t; [ -x $P/$1 ] || { echo "  $1: not in this piglit"; continue; }
        printf '  %-24s ' "$1"; env $(gl4es_env $v) PIGLIT_PLATFORM=glx timeout 30 $P/$t -auto -fbo 2>&1 | grep -E "^PIGLIT:" | tail -1
    done
done
kill $XVFB_PID
