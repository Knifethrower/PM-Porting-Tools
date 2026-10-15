# LÖVE ports on PortMaster's love_11.5 runtime

PortMaster ships LÖVE 11.5 in the control folder (`$controlfolder/runtimes/love_11.5/love.txt` sets
`$LOVE_RUN`, `$LOVE_BINARY`, `$LOVE_GPTK`); a LÖVE port is the game folder + a launcher, nothing compiled.
Launchers to copy from released ports: Sienna's `Sienna.sh` (keys via gptokeyb2),
Zabuyaki's `Zabuyaki.sh` (game reads the pad), Techmino's `Techmino.sh`
(seeds the game's settings on first launch).

- `compat_love010_on_11.lua`: `require` it first in `main.lua` to run a LÖVE 0.10 game on 11:
  0-255 colours, `love.filesystem.exists`, premultiplied alpha for multiply/lighten/darken.
  Also check `newImageFont` (path or ImageData, not an Image), `SpriteBatch:setColor`,
  `ParticleSystem:setColors` and `ImageData` pixels (0-1 now). From Duck Marines.
  `love.audio.newSource(path)` needs a type on 11 (`"static"`/`"stream"`): Orthorobot wraps it
  in its compat.lua. 0.9-era games: `love.graphics.setMode` and `"l"`/`"r"` mouse buttons.
  Since 2026-10-03 the shim also returns `getColor`/`getBackgroundColor` in 0-255 (a library's
  getColor -> setColor round trip had tinted Codename LT's whole frame black) and replaces the
  deprecated `isDirectory`/`isFile` (their warning is drawn on screen), gives `newSource` a default
  type, and restores `Image:getData()` (keeps each image's ImageData; Lonoma's picking uses it).
  `love.graphics.newScreenshot` is gone in 11: copy the game's own render canvas instead
  (as the Codename LT port does).
- Tools (2026-10-03): `add_compat.py <game dir>` (copies the shim, requires it, sets t.version);
  `make_love_release.py <port folder>` builds `release/` (launcher, ini, port.json, gameinfo.xml,
  gamedata via git archive incl. submodules, licenses, patch) from a `port.toml` (format in its
  header; the KłełeAtoms port has an example); `testing_thread.py <release> <title> <url>`
  writes the Discord post from the README controls table. Device loop: `devtools/device/knulli_tmp/`.
- GLES runtime: shader uniforms named like GLSL ES 3 built-ins (`textureSize`) fail to compile,
  and so does implicit int -> float (`float * int uniform`: cast with `float()`, NEON PHASE).
- Games that call `love.joystick.loadGamepadMappings(<bundled db>)` override the firmware's
  mapping: drop the call (NEON PHASE), as with Taisei's gamecontrollerdb.
- `t.window.msaa` above what the GPU offers (Star Phase asked for 32): LÖVE recreates the window and
  on KMSDRM that window gets **no keyboard events at all** (the pad still works). Set it to 0.
- PNGs with a bad chunk CRC load on some desktops but LÖVE 11 refuses them ("invalid CRC
  encountered"): rewrite the chunk CRCs (Star Phase needed it).
- 1 GB devices: add up the decoded image sizes (w*h*4) before trying; Star Phase's 4000 px planets
  got it OOM-killed while loading (stored at 1/4, drawn x4). Copy big games to the device over the
  SMB share, never into /tmp (tmpfs = RAM).
- D-pad: on most handhelds SDL maps it as gamepad buttons; games that read only `getHat` need
  `isGamepadDown('dpleft', ...)` too (Zabuyaki).
- No hardware cursor on KMSDRM: games using the system cursor need their own pointer drawn
  (Techmino: `sysCursor=false`).
- Fixed-size games (`setMode(W*scale, ...)`): keep the fullscreen window and scale uniformly with
  bars (Sienna's `SIENNA_FIT`, Duck Marines' `DUCK_FIT`), and offset any `setScissor`.

## Filling the screen ("fit")

**`fit.lua`** (2026-10-02): one line at the top of `main.lua`, before any compat shim, and
`export PM_FIT=1` in the launcher:

```lua
require("fit").setup(1024, 768, {pointer = true})   -- W x H = the size the game draws at
```

Fullscreen window; everything the game draws to "the screen" goes into a W x H canvas that is
scaled uniformly and centred (black bars, or `bars = "game"` for its background colour; `integer`
for whole-number scales); mouse/touch coordinates, `getDimensions`, `getMode`,
`getDesktopDimensions` and `setMode` are translated so the game sees its own window size; `pointer`
draws an arrow (KMSDRM has no hardware cursor). Games with the mouse module switched off work too.
Options and limits in the file's header.

Tested on the PC (LÖVE 11.5, Xvfb at 640x480, 720x720, 960x544, 1280x720): a test game (canvas,
scissor, mouse events, setMode) and three real games against their hand-written fits: Safety Blanket
(draws into its own canvas and scales it), Orthorobot (0.10 compat shim, own mouse helpers; a
click on its menu works), IYFCT (3:1 strip, no mouse module). Matched within the games' own
animation. On the RG Cube XX (2026-10-04): A Village in the Sky (800x480, pointer; clicks, drag-pan
and zoom with a gptokeyb2 stick mouse) works. Lonoma (640x480 point-and-click with the 0.10 shim)
works too; its object picking needed `Image:getData()` back (now in the shim). Debugging tip: run the
game on the PC first (`love .` in Xvfb, clicks with xdotool)
— an error that the device only shows as "nothing happens" appears in the log at once.

Lessons from writing it: transforming the game's drawing instead (origin/scissor hooks) breaks
games that set their scale while a canvas is active; LÖVE refuses `present()` and
`event.pump()` while any canvas is active, so the stand-in canvas is selected at each frame's
`origin()`; `getMode()`'s flags carry the windowed x/y, which offset the fullscreen window on X;
draw `present()`'s blit with the original `setColor`, the 0.10 shim's 0-255 one made it black.

The seven earlier ports keep their hand-written versions (`ORTHO_FIT`, `SB_FIT`, `TP_FIT`,
`TROSH_FIT`, `IYFCT_FIT`, `SIENNA_FIT`, `DUCK_FIT` in the launchers), which work on the RG Cube XX:
Orthorobot's is the most complete of them.

TROSH (LÖVE 0.8) needed more than the 0.10 shim: `drawq`, `setDefaultImageFilter`, `setIcon`,
`getMode`/`setMode`, `getColor` in 0-255, `newSource` type, `math.mod`
(in the TROSH port's patch).
