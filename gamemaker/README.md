# GameMaker: ASTC for images loaded at runtime (and gmloader-next fixes)

From Daydreamer: Awakened Edition (GMS 1.4.1804, 2026-10). For GameMaker games that are too big
for 1 GB handhelds **because of images they load from disk at runtime** with `sprite_add` /
`background_add`. Texture pages inside data.win are not handled here: use gmtoolkit
(`externalize_textures`, `repack`), as every current PortMaster GameMaker port does.

## The problem

`data.win` texture pages are what gmtoolkit compresses. Some games instead ship folders of PNG
strips and load them per room (Daydreamer: 909 PNGs, 1.1 GB on disk, 4.7 GB as RGBA, up to
~280 MB per boss room). The GMS 1.4 runner keeps **three RGBA copies** of every frame it makes
from such a file: the `CBitmap32` (CPU, for collision masks and `sprite_merge`), the Texture's
upload buffer (never freed, kept for Android context loss) and the GL texture (system RAM on Mali).
Nothing in data.win describes these images, so no data.win tool can compress them.

## What is here

| | |
|--|--|
| `sprite_list.py` | Finds every `sprite_add` / `background_add` with a constant `.png` path in data.win's bytecode and writes the list (`<png> <frames> <removeback> <smooth>`). Run it once on the PC; ship the list with the port. Verified on bytecode 16. |
| `gmsprites/` | `gmsprites <list> <block>`: on the device (first run), writes `<png>.astc` next to each PNG: the frames exactly as the runner cuts and processes them (strip split, `RemoveBackground` / `ImproveBoundary` / `SmoothEdges` reimplemented from the 1.4 runner), ASTC encoded (ARM astcenc), each with the CRC32 of its RGBA pixels. `build.sh` (aarch64, glibc 2.29, and x86_64 for PC tests; not prebuilt here). |
| `gmloader-next/` | Four independent patches for [gmloader-next](https://github.com/JohnnyonFlame/gmloader-next) (522d964; each applies alone, any order), `letterbox.cpp`, `spritehack.cpp`, `make_patches.py` (writes the patches from a pristine checkout), `build.sh` (armhf, bullseye cross like upstream CI). |

### The gmloader-next patches

| Patch | What | Who needs it |
|--|--|--|
| `01-texhack-arm32` | The armhf texture hack hooked `&LoadTextureFromPNG` (the address of a local variable) instead of the function, so gmtoolkit's `.pvr` pages were never used on armhf: text and sprites drew the 2x1 stub's colour. `texture_arm64.cpp` is right. | Every armhf gmloader-next port with externalized textures. Upstream candidate. |
| `02-openal-system` | Builds upstream's own `USE_OPENAL_THUNKS` code (unused since e0fc3af): the runner's OpenAL calls go to the system `libopenal.so.1` and the APK's `libopenal.so` is not loaded. 1.4.1804's APK OpenAL (2015 OpenAL-soft, OpenSL ES / JNI back ends) crashes in `Sound_Prepare`. | Runners that link `libopenal.so` (old 1.4). Newer runners don't. |
| `03-render-size` | `"render_size": "1280x720"` in gmloader.json: the runner draws into an offscreen target of that size (it takes it for the window: `glBindFramebuffer(0)` / `GL_FRAMEBUFFER_BINDING` redirected), shown in the largest area of that shape with a mipmapped downscale and black bars, cleared after each present. Without it, on 720x720 the 1.4 runner stretched its GUI layer over the whole window and drew it at screen size: thin fonts lost whole strokes. Cost: one full-screen pass, ~5 MB. | Games whose GUI is drawn for a size much larger than the screen, or non-16:9 screens. |
| `04-spritehack` | Wraps `sprite_add`, `background_add`, `sprite_collision_mask`, `sprite_merge`. After the original `sprite_add`: open `<png>.astc`, check frame count, sizes and **every frame's CRC32** against the runner's own decoded pixels; if all match, create each frame's GL texture from ASTC right away and free the upload buffer; the CPU bitmaps are freed after the game's `sprite_collision_mask` call or at the end of the step (masks and merges happen in the same Create event as the add). Any mismatch or no ASTC support: the sprite stays as the runner made it. | Games with big runtime-loaded images. **Object offsets are for the GMS 1.4.1804 armeabi-v7a runner** (`CSprite`, `CBackground`, `CBitmap32`, `Texture`; listed in `spritehack.cpp`); check them with Ghidra before using it with another runner. |

## Using it in a port

1. PC: `python sprite_list.py data.win sprites.txt`; read what it skipped (paths built at runtime).
2. Port: gmloader-next built with the patches the game needs (`build.sh 01 02 03 04`), gmloader.json
   with `"disable_texhack": false` (and `"render_size"` if wanted), `tools/gmtoolkit.aarch64` +
   json (pages, repack), `tools/gmsprites.aarch64`, `tools/sprites.txt`.
3. patchscript, in the save_dir: `gmtoolkit --config ... data.win` (then data.win → game.droid),
   `gmsprites.aarch64 sprites.txt 6x6`. Interrupted runs resume (finished packs are skipped).
4. The log shows one `spritehack: <png>: N frame(s) ASTC WxH` line per converted load and a
   reason for every one left as RGBA.

## gmtoolkit and this, side by side

| | gmtoolkit | here |
|--|--|--|
| Texture pages in data.win | yes (PNG/QOI pages → `.pvr`, repack, keep-inline) | no (left to gmtoolkit) |
| Images loaded from disk at runtime | no (not in data.win) | yes |
| Runner's CPU copies (bitmaps, upload buffers) | — | freed |
| Audio, flags, shaders, GML code | yes | no |
| Changes the loader | no (uses the texhack) | yes (patches above) |

## Why not native compression

GameMaker only gained GPU texture compression (ASTC/BC, chosen per texture group at compile time)
in 2024.4; it is a developer-side build option, so it can't be applied to a shipped game, and it
never covers `sprite_add` of external files: the runner always decodes those to RGBA. The 1.4
runner knows a PVR container for texture pages (an iOS PVRTC path), no ASTC, and Mali GPUs can't
sample PVRTC anyway.

## Measured (RG Cube XX, Knulli, 972 MB, 2026-10-04)

Same scripted run (intro → title → new game → cutscene → tutorial level), RAM used = drop in
MemAvailable: old port (stock gmloader, RGBA) 476 MB at the title and dies at the first cutscene
load (also with its 2 GB repack patch: 403 MB, dies); with this + ASTC pages: 198 MB at the title,
410 MB peak, ~276 MB in the level (droidports gmloader with the same changes: 158 / 396 / ~231).
First-run setup for 861 images: 15 min. Details: `C:\Claude\DaydreamerAE\ASTC-NOTES.md`.

## Licences

`gmsprites.c` and `sprite_list.py`: own code, 0BSD (`gmsprites/LICENSE.txt`); `astc_shim.cpp` same;
astcenc Apache-2.0; `stb_image.h` MIT/public domain. The gmloader-next patches, `letterbox.cpp` and
`spritehack.cpp` are changes to gmloader-next and ship under its licence (GPL-2.0, `gmloader-next/LICENSE`).

Not in this repository: `stb_image.h` (get it from https://github.com/nothings/stb and put it in `gmsprites/`) and
ARM astc-encoder (https://github.com/ARM-software/astc-encoder); `build.sh` says where it expects them.
