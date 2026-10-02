# LÖVE ports on PortMaster's love_11.5 runtime

PortMaster ships LÖVE 11.5 in the control folder (`$controlfolder/runtimes/love_11.5/love.txt` sets
`$LOVE_RUN`, `$LOVE_BINARY`, `$LOVE_GPTK`); a LÖVE port is the game folder + a launcher, nothing compiled.
Launchers to copy: `C:\Claude\Sienna\release\Sienna.sh` (keys via gptokeyb2),
`C:\Claude\Zabuyaki\release\Zabuyaki.sh` (game reads the pad), `C:\Claude\Techmino\release\Techmino.sh`
(seeds the game's settings on first launch).

- `compat_love010_on_11.lua`: `require` it first in `main.lua` to run a LÖVE 0.10 game on 11:
  0-255 colours, `love.filesystem.exists`, premultiplied alpha for multiply/lighten/darken.
  Also check `newImageFont` (path or ImageData, not an Image), `SpriteBatch:setColor`,
  `ParticleSystem:setColors` and `ImageData` pixels (0-1 now). From Duck Marines.
  `love.audio.newSource(path)` needs a type on 11 (`"static"`/`"stream"`): Orthorobot wraps it
  in its compat.lua. 0.9-era games: `love.graphics.setMode` and `"l"`/`"r"` mouse buttons.
- GLES runtime: shader uniforms named like GLSL ES 3 built-ins (`textureSize`) fail to compile,
  and so does implicit int -> float (`float * int uniform`: cast with `float()`, NEON PHASE).
- Games that call `love.joystick.loadGamepadMappings(<bundled db>)` override the firmware's
  mapping: drop the call (NEON PHASE), as with Taisei's gamecontrollerdb.
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
animation. Not yet run on a device.

Lessons from writing it: transforming the game's drawing instead (origin/scissor hooks) breaks
games that set their scale while a canvas is active; LÖVE refuses `present()` and
`event.pump()` while any canvas is active, so the stand-in canvas is selected at each frame's
`origin()`; `getMode()`'s flags carry the windowed x/y, which offset the fullscreen window on X;
draw `present()`'s blit with the original `setColor`, the 0.10 shim's 0-255 one made it black.

The seven earlier ports keep their hand-written versions (`ORTHO_FIT`, `SB_FIT`, `TP_FIT`,
`TROSH_FIT`, `IYFCT_FIT`, `SIENNA_FIT`, `DUCK_FIT` in the launchers), which work on the RG Cube XX:
`C:\Claude\Orthorobot\tools-src\ortho_port.py` is the most complete of them.

TROSH (LÖVE 0.8) needed more than the 0.10 shim: `drawq`, `setDefaultImageFilter`, `setIcon`,
`getMode`/`setMode`, `getColor` in 0-255, `newSource` type, `math.mod`
(`C:\Claude\Trosh\tools-src\trosh_port.py`).
