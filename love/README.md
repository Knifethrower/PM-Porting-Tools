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

Seven ports hand-wrote the same patch: fullscreen window, the game's fixed-size view scaled
uniformly and centred with black bars, mouse coordinates mapped back, a drawn pointer (KMSDRM has
no hardware cursor), switched on by a launcher variable (`ORTHO_FIT`, `SB_FIT`, `TP_FIT`,
`TROSH_FIT`, `IYFCT_FIT`, `SIENNA_FIT`, `DUCK_FIT`). Copy the closest one:
- `C:\Claude\Orthorobot\tools-src\ortho_port.py`: the most complete (view transform, mouse
  helpers, drawn pointer).
- `C:\Claude\Safety Blanket\tools-src\safetyblanket_port.py`: games that draw into their own canvas
  and scale it.
- `C:\Claude\IYFCT\tools-src\0001-fit-to-screen.patch`, `C:\Claude\Sienna\tools-src\0001-handheld-fit-and-confirm.patch`:
  scissor to the game area.

A generic drop-in `fit.lua` was started and parked unfinished (not included): a transform-based
version broke games that set their scale while a canvas is active (Safety Blanket); the
render-to-canvas version still trips LÖVE's "no canvas active during present/event.pump" rules.

TROSH (LÖVE 0.8) needed more than the 0.10 shim: `drawq`, `setDefaultImageFilter`, `setIcon`,
`getMode`/`setMode`, `getColor` in 0-255, `newSource` type, `math.mod`
(`C:\Claude\Trosh\tools-src\trosh_port.py`).
