# GameMaker

How GameMaker games run on PortMaster (an Android runner loaded on Linux), how to make them fit
1 GB handhelds with gmtoolkit, and what to do when the memory goes into images the game loads from
disk at runtime. Tools and the gmloader-next patches: [gamemaker/](../../gamemaker/)
([README](../../gamemaker/README.md)).

## How a GameMaker port works

A GameMaker game is a runner (the engine) plus `data.win` (bytecode, texture pages, sounds) and
loose files (audio groups, included files). PortMaster ports use the **Android runner**
(`libyoyo.so` of the same GameMaker version) and a loader that runs it on Linux with
a small Android/Java stand-in:

- **gmloader** (droidports, armhf) and **[gmloader-next](https://github.com/JohnnyonFlame/gmloader-next)**
  (armhf and aarch64), configured by a `gmloader.json`.
- The game's `data.win` from the user's copy (Steam, GOG) becomes `game.droid`; the runner version
  must match what the game was built with. Lords of Exile (GameMaker 2023.4, bytecode 17) runs on
  a 2023.4 aarch64 runner; Daydreamer: Awakened Edition (GMS 1.4.1804, bytecode 16; work in
  progress, not released) uses the 1.4.1804 armeabi-v7a runner on armhf.
- Conversion happens on the device at first launch, in a patchscript run by PortMaster's patcher
  (see [packaging](../packaging.md)). Nothing of the game is shipped.

Why not the same trick for Unity: GameMaker's runner needs only a small Java surface, Unity's much
more (see [choosing an approach](../choosing-an-approach.md)).

## gmtoolkit first

[gmtoolkit](https://github.com/JeodC/gmtoolkit) is what every current PortMaster GameMaker port
uses to make `data.win` fit: texture pages to ASTC `.pvr` files (loaded through the loader's
"texhack"), repacking, audio to Ogg, flags. Start every port with it.

Lords of Exile, a typical case (Oct 2026): 425 MB of WAV music in 9 audio groups and 13 texture
pages of 2048x2048. First-run gmtoolkit: audio to Ogg at 96 kbps = 30 MB, textures to ASTC 4x4 =
56 MB.

| Device | Setup time | Result |
|--|--|--|
| RG Cube XX (Knulli, 972 MB) | 408 s (ASTC "medium"; later switched to "fast", not re-timed) | Title and stage 1 at 60 fps, 206 MB RSS, 620 MB free |
| RG351P (ROCKNIX, RK3326) | 149 s for the full setup after switching audio to VBR | Stage 1 at 57.9 fps average (min 54), 160 MB RSS |

Lessons from it:

- **Use VBR for the audio conversion** (bitrate 0). gmtoolkit's ABR mode was ~5x slower on the
  device; setup time matters on RK3326-class CPUs.
- The ASTC encoder preset trades first-run time against quality; the Lords of Exile port moved
  from "medium" to "fast" to shorten the setup.
- An earlier, unsubmitted port of the same game (by changgu21) converted only the audio and had slowdowns and RAM problems:
  convert both.

## When the memory is not in data.win: runtime-loaded images

Some games load big images from disk with `sprite_add` / `background_add` instead of keeping them
on texture pages. gmtoolkit cannot touch those: nothing in `data.win` describes them.

Daydreamer: Awakened Edition (the author's port, work in progress, not released) ships 909 PNG strips (1.1 GB on disk, 4.7 GB as RGBA, up to ~280 MB per
boss room). Decompiling the 1.4 runner showed that it keeps **three RGBA copies** of every frame
made from such a file:

1. a CPU bitmap (for collision masks, `sprite_merge`, `sprite_save`),
2. the texture's upload buffer, **never freed** (kept for Android context loss),
3. the GL texture, which on Mali is system RAM too.

The port's answer, now in [gamemaker/](../../gamemaker/):

- `sprite_list.py` finds every `sprite_add` / `background_add` with a constant path in the
  bytecode (frame count and flags too), once on the PC.
- `gmsprites` on the device (first run) reproduces exactly what the runner does to each image
  (strip split, background removal, edge smoothing) and writes ASTC frames with a CRC32 of each
  frame's RGBA pixels.
- The `04-spritehack` gmloader-next patch wraps the runner's builtins: after the original
  `sprite_add`, it checks frame count, sizes and **every frame's CRC32** against the runner's own
  pixels, then uploads ASTC and frees the upload buffer; the CPU bitmaps go after the game's
  collision-mask call or at the end of the step. Any mismatch, or a GPU without ASTC: the sprite
  stays as the runner made it, with a log line. The CRC check is what makes this safe on a game you
  cannot fully test.
- The runner's object offsets are version-specific (listed in `spritehack.cpp` for 1.4.1804): check
  them again before using it with another runner.

### Measured (RG Cube XX, Knulli, 972 MB, Oct 2026)

Same scripted run (intro, title, new game, cutscene, tutorial level), front end stopped; "used" =
drop in MemAvailable, which includes GPU memory:

| | Title | Cutscene load | Tutorial level |
|--|--|--|--|
| This port (gmloader-next, ASTC pages + runtime images; setup by the port's own converter) | 198 MB | peak 410 MB | ~276 MB |
| Same changes on droidports gmloader | 158 MB | peak 396 MB | ~231 MB |
| Old port with its 2 GB repack patch | 403 MB | **died** (61 MB left) | |
| Old port, stock data.win | 476 MB | **died** | |

The old variants did not log a kernel OOM kill: the loader's allocations failed and it exited. If a
GameMaker port just disappears at a level load, look at MemAvailable, not only dmesg. First-run
setup for 10 pages and 861 images: 15 min (896 s), 527 MB of ASTC packs on disk (measured with the
port's own converter; gmtoolkit + gmsprites not re-timed).

## gmloader-next patches (from the Daydreamer port)

Four independent patches in [gamemaker/gmloader-next/](../../gamemaker/gmloader-next/); one line
each here, details in the README:

- **`01-texhack-arm32`**: on armhf the texture hack hooked the address of a local variable instead
  of the function, so gmtoolkit's `.pvr` pages were never used and text drew in the 2x1 stub's
  colour. Affects every armhf gmloader-next port with externalized textures (upstream candidate).
- **`02-openal-system`**: the 1.4.1804 runner's own 2015 Android OpenAL crashed in `Sound_Prepare`;
  route its calls to the system `libopenal.so.1` instead. Newer runners do not link it.
- **`03-render-size`**: draw into an offscreen target of a fixed size and show it letterboxed with a
  mipmapped downscale. On 720x720 the 1.4 runner letterboxed the room but stretched its GUI layer over
  the whole window and drew it at screen size, so the game's thin 1280x720 pixel font lost whole
  strokes at 56 % scale. Cost: one full-screen pass and ~5 MB.
- **`04-spritehack`**: the runtime-image ASTC path above.

Format detail if you work with both loaders: droidports gmloader reads a PVR's width at 0x18 and
height at 0x1C; gmloader-next uses the standard order (height first) and looks for the files in
`<save_dir>/textures/`.

## Why not GameMaker's own texture compression

GameMaker gained GPU texture compression (ASTC/BC per texture group) only in 2024.4, as a build
option for the developer: it cannot be applied to a shipped game and never covers `sprite_add` of
external files (always decoded to RGBA). The 1.4 runner knows a PVR container for texture pages
(an iOS PVRTC path) and no ASTC, and Mali GPUs cannot sample PVRTC.

## Testing on the PC

qemu-arm with Mesa llvmpipe runs the loader and the runner; llvmpipe exposes ASTC
LDR, so the ASTC paths render the same frames as on the device. RSS on the PC means nothing here:
llvmpipe decodes ASTC back to RGBA. Measure memory on a device, with a scripted run that reaches the
heaviest scenes (cutscene and level loads), and compare against the old port on the same device.
More in [testing and debugging](../testing-and-debugging.md) and
[performance and memory](../performance-and-memory.md).
