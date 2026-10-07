# Licenses

Everything is [0BSD](LICENSE) except the paths below, which keep the license they were already
released under in a shipped port, or the license of the project they patch. Each of these folders
has its own `LICENSE` file.

| Path | License | Why |
|--|--|--|
| `shim/glespass-gonehome/`, `shim/glespass-nitw/`, `shim/glespass-test/` | MIT | shipped as glespass in the Night in the Woods and Gone Home ports |
| `unity/unityport/`, `unity/converters/unity5-unitypy/` | MIT | the Night in the Woods / Gone Home setup tools they came from |
| `unity/converters/unity4/` (the Python tools), `unity/patching/` | MIT | shipped as tools in the Usagi Yojimbo port. Exceptions, 0BSD: `patch_input_axes.py`, `tex_inventory.py` (Ittle Dew, never shipped), `patching/dnlib-example/` |
| `unity/converters/unity4/unity4shrink/` | public domain (Unlicense) | shipped that way in the Ittle Dew port |
| `shim/xstub/` | MIT | shipped that way in the Ittle Dew port |
| `box64/`, `wine/box64/` | MIT | patches to [box64](https://github.com/ptitSeb/box64) |
| `devtools/pc-testing/gl4es-mali/` | MIT | patches to [gl4es](https://github.com/ptitSeb/gl4es) (`test_world.sh` there is 0BSD) |
| `gles/gl1es-cube/` | zlib | published that way with Cube in Knifethrower/cube-pm |
| `allegro/` | zlib | like [Allegro](https://github.com/liballeg/allegro5), which the patch and the stub belong to |
| `machismo/patches/` | GPLv3 | patches to [machismo](https://github.com/bmdhacks/machismo) |
| `gamemaker/gmloader-next/` (the patches, `letterbox.cpp`, `spritehack.cpp`) | GPL-2.0 | changes to [gmloader-next](https://github.com/JohnnyonFlame/gmloader-next) (`make_patches.py`, `build.sh` there are 0BSD) |
| `mpv/` (`context_sdl.c`, `gl-sdl.patch`) | LGPL-2.1-or-later | changes to [mpv](https://github.com/mpv-player/mpv) (`build_libmpv.sh` is 0BSD) |
| `devtools/gl4es-test/patches/` | MIT | patches to [gl4es](https://github.com/ptitSeb/gl4es) (`make_candidate.py` there is 0BSD) |

`shim/sysvsem/` and `devtools/profiling/fpslog/` say 0BSD in their own headers, matching the rest.

Not included, get them upstream: `stb_dxt.h` for unity4shrink and `stb_image.h` for gmsprites
([nothings/stb](https://github.com/nothings/stb), public domain / MIT); ARM's
[astc-encoder](https://github.com/ARM-software/astc-encoder) (Apache-2.0) for unity4shrink, glxsdl's
`astc_dec.cpp` and gmsprites.
