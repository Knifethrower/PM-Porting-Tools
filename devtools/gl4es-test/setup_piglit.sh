#!/bin/bash
# Builds piglit with GLX (Ubuntu's package has no GLX, so it cannot load gl4es's libGL). ~15 min.
# Needs (as root): apt install libwaffle-dev waffle-utils libx11-dev libxrender-dev libxkbcommon-dev libegl-dev
#   libgl-dev libglx-dev python3-numpy python3-mako python3-lxml libdrm-dev libgbm-dev libwayland-dev
#   wayland-protocols libpng-dev glslang-tools ninja-build
set -e
GT=${GT:-$HOME/gl4es-test}
cd "$GT"
[ -d piglit-src ] || git clone -q --depth 1 https://gitlab.freedesktop.org/mesa/piglit.git piglit-src
cd piglit-src && mkdir -p build && cd build
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release -DPIGLIT_USE_WAFFLE=ON -DPIGLIT_BUILD_GLX_TESTS=ON -DPIGLIT_BUILD_GL_TESTS=ON \
  -DPIGLIT_BUILD_GLES1_TESTS=OFF -DPIGLIT_BUILD_GLES2_TESTS=OFF -DPIGLIT_BUILD_GLES3_TESTS=OFF -DPIGLIT_BUILD_CL_TESTS=OFF \
  -DPIGLIT_BUILD_VK_TESTS=OFF -DPIGLIT_BUILD_DMA_BUF_TESTS=OFF > cmake.log
nice -n 5 ninja -j8 > ninja.log
echo "piglit built: $GT/piglit-src (commit $(git -C .. log -1 --format=%h))"
