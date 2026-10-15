# LÖVE

How to port a game written for LÖVE (Lua, any version from 0.8 to 11) to PortMaster's `love_11.5`
runtime: version differences, input, filling the screen, memory and testing. The tools and the full
list of API fixes are in [love/](../../love/); this page is the overview to read first.

## What a LÖVE port is

PortMaster ships LÖVE 11.5 in its control folder (`runtimes/love_11.5`, a file there sets
`$LOVE_RUN`, `$LOVE_BINARY` and `$LOVE_GPTK` for the launcher). A LÖVE port is the game's folder (or
`.love` file), a launcher and a gptokeyb2 mapping: nothing is compiled, nothing is downloaded
(merged LÖVE ports have `"runtime": []` in port.json), and the port is ready to run on every
firmware that has PortMaster (the author's ports were tested on Knulli only).

- Launcher patterns from the author's ports (see [love/README.md](../../love/README.md) for names):
  keys through gptokeyb2 (the pad becomes keyboard keys), the game reading the pad itself
  (gptokeyb2 only for the Select+Start quit), and a launcher that seeds the game's settings on first
  launch (language, pointer).
- Saves: point the game's save directory into the port folder (the author's ports keep it in
  `<port>/conf`), so saves survive firmware updates and are easy to back up.
- [make_love_release.py](../../love/) builds the release folder (launcher, gptokeyb2 ini,
  port.json, gameinfo.xml, the game via `git archive` including submodules, licences, the port's
  patch) from one `port.toml`. General packaging rules: [packaging](../packaging.md).

The runtime is also a good base for new projects: the author's Home Assistant client and a chess
game with a bundled Stockfish binary (UCI over FIFOs from a `love.thread`) are plain LÖVE
projects. Apps are on [apps and video](apps-and-video.md).

## Step 1: which LÖVE version was it made for

Look at `conf.lua` (`t.version`), the README and the API the code uses. Games made for 11.x
usually run unchanged (Temple of Anput ran with no change at all). Older games need a compatibility
layer, because LÖVE 11 changed several things without an error you would notice at once:

| Made for | What to do |
|--|--|
| 11.x | Run it. Check shaders (below), input and screen fit. |
| 0.10 | `require` [compat_love010_on_11.lua](../../love/) first in `main.lua` (`add_compat.py` does it and sets `t.version`). |
| 0.9 | The same shim plus `love.graphics.setMode` and the `"l"`/`"r"` mouse button names. |
| 0.8 | The shim plus more stand-ins: `drawq`, `setDefaultImageFilter`, `setIcon`, `getMode`/`setMode`, `math.mod` (TROSH needed all of these). |

The changes that cost the most time, all handled by the shim (full list in the README):

- **Colours are 0-1 in LÖVE 11, 0-255 before.** It is not only `setColor`: `getColor` and
  `getBackgroundColor` must return 0-255 too, or a library's `getColor` -> `setColor` round trip
  tints the whole frame black (Codename LT). `ImageData` pixels are 0-1 as well: games that read
  their levels from PNG colour maps (Orthorobot) compare against 0-255 values.
- Blend modes multiply, lighten and darken need premultiplied alpha on 11.
- `love.filesystem.exists`, `isDirectory`, `isFile` are gone or deprecated; the deprecation
  warning is **drawn on screen**, so replace them rather than ignore it.
- `love.audio.newSource(path)` needs a type (`"static"` or `"stream"`).
- `Image:getData()` is gone. A point-and-click game that picks objects by their pixels (Lonoma)
  silently stops reacting to clicks; the shim keeps each image's ImageData and restores the method.
- `love.graphics.newScreenshot` is gone: copy the game's own render canvas instead.
- Things the shim cannot know: `newImageFont` wants a path or ImageData (not an Image),
  `SpriteBatch:setColor`, `ParticleSystem:setColors`, a 0.10 main loop (`Canvas:clear`,
  `window.isOpen`).

## Step 2: shaders on GLES

The handhelds run LÖVE on OpenGL ES, which is stricter than desktop GL. Two errors seen in the
author's ports, both shown as LÖVE's error screen at start or when a level loads:

- A uniform named like a GLSL ES 3 built-in (`textureSize`) does not compile: rename it (Zabuyaki).
- No implicit int -> float: `float * int uniform` and `float / 2` fail; write `float(x)` and `2.0`
  (NEON PHASE, Boxclip).

Search every `.glsl` file and every string passed to `love.graphics.newShader` for these before the
first device run.

## Step 3: input

- **gptokeyb2 or the game's own pad code.** If the game supports a gamepad well, let it read the pad
  and use gptokeyb2 only for the quit hotkey. Otherwise map the pad to the keys the game expects
  (or to a mouse for point-and-click and touch games).
- **D-pad**: on most handhelds SDL reports it as gamepad buttons, not a hat. Games that only read
  `getHat` have a dead D-pad: add `isGamepadDown('dpleft', ...)` (Zabuyaki).
- **Bundled controller databases**: a game that calls `love.joystick.loadGamepadMappings(<its own
  file>)` overrides the firmware's mapping. Drop the call (NEON PHASE).
- **Raw joystick callbacks** with Xbox button numbers do the wrong thing on a handheld pad; switch
  them off and drive the game through gptokeyb2 keys (Codename LT).
- **Dormant keyboard modes**: some mouse-driven games already have a keyboard pointer mode or a
  fullscreen scaling mode that is just not enabled. Turning it on is less work than a new input
  layer (KłełeAtoms: arrow-key pointer, Enter clicks). Check that clicks are not converted twice
  when scaled.
- **No hardware cursor on KMSDRM** (the display mode most firmwares use for SDL games): games that
  rely on the system cursor need a drawn pointer (Techmino has its own, `sysCursor=false`;
  [fit.lua](../../love/) can draw one).
- **`t.window.msaa` above what the GPU offers** (Star Phase asked for 32): LÖVE recreates the window,
  and on KMSDRM the new window gets **no keyboard events at all** while the pad still works. Set it
  to 0.

## Step 4: filling the screen

Screens are 640x480, 720x720, 960x544, 1280x720 and more (see
[devices and firmwares](../devices-and-firmwares.md)). Many LÖVE games open a fixed-size window
(`setMode(W*scale, H*scale)`), which on a fullscreen KMSDRM display ends up in a corner or cropped.

**Use [fit.lua](../../love/)** (since Oct 2026): one `require("fit").setup(W, H, {...})` at the top
of `main.lua`, before any compat shim, and `export PM_FIT=1` in the launcher. The game draws into a
W x H canvas that is scaled uniformly and centred (black bars, the game's background colour, or
whole-number scales); mouse and touch coordinates, `getDimensions`, `getMode`,
`getDesktopDimensions` and `setMode` are translated so the game sees its own window size. Options in
the file's header.

Evidence: tested on the PC (LÖVE 11.5, Xvfb at 640x480, 720x720, 960x544, 1280x720) against three
hand-written fits, then on the RG Cube XX (Oct 2026) with A Village in the Sky (800x480: pointer,
clicks and drag-pan with a stick mouse, zoom on L1/R1), Lonoma (640x480 with the 0.10 shim) and 90 Second Portraits
(320x240 at a whole-number 2x).

Lessons from writing it, useful if a game needs its own fit:

- Transforming the game's drawing instead (origin and scissor hooks) breaks games that set their
  scale while a canvas is active. Redirecting "the screen" to a canvas is more robust.
- LÖVE refuses `present()` and `event.pump()` while any canvas is active, so the stand-in canvas is
  selected at each frame's `origin()`.
- Offset any `setScissor` the game does.
- `getMode()`'s flags carry the windowed x/y, which offset the fullscreen window on X11.
- Draw the final blit with the original `setColor`: the 0.10 shim's 0-255 version made it black.

Some games scale themselves and only need their own fullscreen option switched on. On square
screens, games designed for 16:9 may crop their art at the sides by design (NEON PHASE title,
Techmino menus): note it in the README rather than fight the game's layout.

## Step 5: memory

LÖVE games are usually small (A Village in the Sky and Temple of Anput: ~115 MB RSS), but images are decoded to
RGBA in RAM and on Mali GPUs GPU memory is system RAM too.

- **Add up the decoded image sizes (w x h x 4) before the first device run.** Star Phase's 31
  planet PNGs of 2000-4000 px were ~800 MB decoded and the game was OOM-killed while loading on the
  RG Cube XX (972 MB, Oct 2026). Stored at 1/4 size and drawn 4x larger it loads in ~45 s and runs
  at ~370 MB RSS.
- **PNGs with a bad chunk CRC** load on some desktops but LÖVE 11 refuses them ("invalid CRC
  encountered"): rewrite the chunk CRCs (17 files in Star Phase).
- Copy big games to the device over the SMB share or SSH, never into `/tmp` for a long test: `/tmp`
  is RAM (tmpfs) and takes memory from the game.

More on RAM budgets: [performance and memory](../performance-and-memory.md).

## Testing

- **Run the game on the PC first**: `love .` in Xvfb, clicks and keys with xdotool. An error that
  the device shows only as "nothing happens" appears in the PC log at once (Lonoma's picking).
- On the device: [knulli_tmp](../../devtools/device/knulli_tmp/) runs a LÖVE folder on love_11.5
  before it has a launcher, with a virtual pad and keyboard on FIFOs for unattended runs. More in
  [testing and debugging](../testing-and-debugging.md).
- Blind driving with scripted presses reaches menus and the first level reliably; deeper parts
  (battles, mini-games, painting with a stick) need a human tester. Say in the README and the
  testing thread what was and was not checked. `testing_thread.py` writes the testing-thread post
  from the README's controls table.

## Licences

LÖVE games are often open source code with assets under other terms. Check the art, music and fonts
before starting, not after: in the author's batches several finished ports were held back, one LÖVE
port because its art uses other companies' characters, and Godot 3 ports because a font, a tileset
or sound clips had no stated licence. A licence that covers only the code does not make a port
submittable. PortMaster's rules: [packaging](../packaging.md#licences).
