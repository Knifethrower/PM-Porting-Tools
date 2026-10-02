# OpenGL 1.x on GLES 2: native layers to copy

Games with source that draw with fixed-function OpenGL get a native GLES 2 layer compiled into the
game, never gl4es (knowledge §1). Both layers take the game's `gl*` calls (macros rename them to
`gl1_*`), load the ES 2.0 entry points with `SDL_GL_GetProcAddress` (no GL library linked or
bundled) and generate shaders for the fixed-function state. Copy the one closer to the game and cut
or extend it to the subset the game actually uses; unsupported calls log once (`UNSUPPORTED`).

| Folder | From | Covers | Draws |
|--|--|--|--|
| `gl1es-c/` (C, 1360 lines) | Bugdom 2 (2026-10-01) | lighting (4 directional lights, colour material, normalize), 2 texture units (modulate/add), sphere-map texgen, texture matrix, linear fog, alpha test, client arrays + glDrawElements, glBegin/glEnd, texture format conversion (BGRA 1555, RGB) | meshes go to the GPU untransformed; glBegin/glEnd batched per state run; uniforms uploaded only when changed |
| `gl1es-cube/` (C++, 726 lines) | Cube (2026-09-30) | glBegin/glEnd strips, fog, overbright, lines | every vertex transformed on the CPU into one array, one draw per state change (1,000-2,000 calls a frame became a few dozen on Mali) |

ES cannot read the depth buffer. Both answer the game's depth reads with a 1-pixel draw that
writes depth as a colour and reads it back: `gl1_DepthLess()` (Bugdom 2's lens flare: is the sun
hidden) and `gl1_readdepth()` (Cube: the depth under the crosshair).

Hooking it into a game (Bugdom 2, CMake option `USE_GLES2`): include `gl1es.h` after
`SDL_opengl.h` in the game's common header, ask SDL for an ES 2.0 context, call `gl1_init()` after
the context exists, replace depth reads with the helper. Keep the desktop GL build as the
reference: compare frames with `devtools/native-testing/replay_compare.sh` (Bugdom 2 matched the
desktop GL build on all 10 levels on PC; ~60 fps on the RG Cube XX).

Licenses: `gl1es-c` 0BSD (own code, written for the port); `gl1es-cube` zlib, as published in
Knifethrower/cube-pm with Cube.
