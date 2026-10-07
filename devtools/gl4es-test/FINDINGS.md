# gl4es findings from the test bed

Library under test (`ship`): gl4es a744af14 (= Westonpack's "v1.1.7 built on Mar 10 2025", the base of the gl4es
the Unity 4 ports ship) with the two fixes the ports already carry (ARB id recycling, `GL_UNSIGNED_SHORT` read).
Reproduced on PC: gl4es x86_64 on Mesa llvmpipe GLES 2 with the Mali capability profile, Mesa desktop GL as the
reference. "head" = upstream master of 2026-10-04 (see the head column once its run is in).
**Ports** = does the real Unity 4 code path hit it: the corpus of 803 ARB programs from Ittle Dew, Teslagrad, Usagi
and the Unity 4.7.2 built-ins (`extract_arb.py`), and apitrace recordings of Ittle Dew and Teslagrad replayed
through gl4es (`trace/`).
**Fix** = in `patches/candidate-fixes.patch` (made by `patches/make_candidate.py`, variant `fix`).

## Results of the overnight run (2026-10-05)

Own tests + Unity corpus, 13,940 tests, same reference, edge-tolerant comparison (`results/variants-table.md`):

| build | passing | crashing | AddressSanitizer / UBSan reports |
|--|--|--|--|
| ship (what the ports ship, x86_64 rebuild) | 6,467 | 50 (14 on real Unity programs) | ~490 tests: heap overflows in the ARB parser (pushArray, appendString, state.light), fragment locals, pixel conversion |
| head (upstream master 2026-10-04 + the ports' 2 fixes) | 6,472 | 78 | (not run) every bug below is still there; 26 extra crashes: `glMultiDrawArrays` with quads (looks like stack corruption, after upstream's Jan 2026 MultiDraw changes) |
| fix (ship + `patches/candidate-fixes.patch`) | 6,792 (+325, none lost) | 29 (0 on Unity programs) | only the unfixed draw-path ones (21, 5) and harmless shift/alignment UB |

Many remaining failures are coverage gaps rather than bugs that bite (16: source types gl4es never converts) or
known limits (7, 12-14). Teslagrad's real call stream: `ship` aborts at startup, `fix` replays clean. Piglit:
unchanged by the fixes (paths it hardly exercises). **Nothing here is device-tested yet.**

## Summary

| # | Bug | Ports | Fix |
|--|--|--|--|
| 10 | ARB parser heap overflow (`resize` returns bytes as element count): every longer ARB program corrupts the heap | **yes**: 5 Unity built-in programs, Teslagrad's real call stream crashes in `glProgramStringARB` | yes, 1 line |
| 19 | fragment programs with more than 24 `program.local` entries overflow `frg_progloc[24]` in the program struct; locals 24+ unusable | **yes**: Unity's shadow-collector program uses 27 (all 4 games) | yes |
| 18 | ARB→GLSL output written with `strcpy` whatever the given length: heap write past the output buffer | possible: the faulty call declares every PARAM array of ≤ 10 entries (Unity uses many); no corpus program hit the buffer end | yes |
| 1 | immediate-mode draws use the NEXT ARB program / program parameters (glBegin/glEnd merging) | likely (Unity GL.Begin: Blit, image effects) | yes (flush), or `LIBGL_BEGINEND=0` |
| 7b | `OPTION ARB_fragment_program_shadow` unknown: program rejected | **yes**: 42 Unity shadow programs | no |
| 5 | drawing with a rejected ARB program enabled crashes (NULL shader source) | yes, together with 7b | no |
| 12 | ARB vertex program + fixed-function fragment stage draws nothing (`_gl4es_Color` declared twice) | unlikely (Unity pairs vp+fp) | no |
| 11 | `GL_UNPACK_ROW_LENGTH` ignores `GL_UNPACK_ALIGNMENT` for the row stride | no (Unity sets row length 0) | yes |
| 20 | after the row-length/skip repack, conversions still apply the app's alignment: rows misread, heap over-read | no | yes |
| 2 | ARB `SIN` produces truncated GLSL | no | yes |
| 3 | heap overflow on every `state.light[n].*` binding | no | yes |
| 4 | `result.color.front` / `.back` alone crash the parser | no | yes |
| 6 / 17 | `glMultiTexCoord*` inside glBegin/glEnd dropped for units without texturing even when a GLSL/ARB program reads them; generic `glVertexAttrib*` inside glBegin/glEnd not recorded at all | probably not (Unity's immediate mode uses arrays) | multitexcoord yes; attribs no |
| 21 | `glPolygonMode(GL_LINE)` path reads one vertex past client/VBO arrays; `glMultiDrawArrays` with a zero count dereferences NULL (upstream master: crashes worse) | no | no |
| 13 | `glShadeModel(GL_FLAT)` ignored for quads, quad strips, fans, triangle strips, polygons | no | no |
| 14 | ARB vp translation: `vertex.fogcoord` swizzles, `result.pointsize`, `result.fogcoord`, `ARB_position_invariant` fail to compile | no | no |
| 7 | other valid ARB syntax rejected (list below) | no | no |
| 15 | `glGetIntegerv(GL_VERTEX_ARRAY_SIZE/TYPE/...)` answer 0 (apitrace records client arrays as size 0) | tools only | no |
| 16 | pixel transfer gaps: BYTE/SHORT/INT/UINT/FLOAT/HALF sources, RED/GREEN/BLUE formats, many readback / glGetTexImage combos | no (Unity uses RGBA/BGRA/ALPHA bytes, DXT) | no |

## Details

### 10. ARB parser heap overflow (the most important one for the ports)
- `arbhelper.c` `resize()`: `*cap = newSize;` stores the new size in **bytes** into a capacity counted in
  **elements**. After the first growth an array of pointers believes it has 8 times its real room, and
  `pushArray()` writes past the end as soon as a program has more than ~72 entries in one list.
- AddressSanitizer: heap-buffer-overflow in `pushArray` (arbhelper.c:34) from `parseToken` (arbparser.c:2988,
  3755). Without ASan: `free(): invalid pointer`, `corrupted size vs. prev_size`, `double free` in
  `glProgramStringARB` / later frees.
- Unity: 5 distinct programs crash it (Unity built-ins present in all four games: 034330b345 fp, dc5242cca3 vp,
  cf8a131481 vp; Teslagrad d47e337b89 fp, 1945dcfd23 fp). The **Teslagrad replay aborts at startup** in
  `glProgramStringARB` with `free(): invalid pointer`. On the handhelds' glibc it need not abort at once: silent
  heap corruption, i.e. random crashes later. Candidate explanation for unexplained Unity 4 crashes (e.g. the
  Ittle Dew quit-time SIGSEGV on a stale pointer), not proven.
- Fix: `*cap = newSize / esize;`. Unchanged in upstream master.

### 19. More than 24 fragment-program locals: struct overflow
- `config.h` `MAX_FRG_PROG_LOC_PARAMS 24`, but programs using more are accepted. `builtin_CheckUniform`
  (fpe.c:2110) fills `frg_progloc[i]` for every declared local: 3 ints past the array into the program struct
  (UBSan: index 24 out of bounds for `GLint [24]`), and locals 24-26 cannot be set (`glProgramLocalParameter`
  index check). Unity program 17cd584726 (`PARAM c[28] = { program.local[0..26], ... }`, the screen-space shadow
  collector) in all four games. Fix: 64 slots + bound the loops (vertex side too).

### 18. ARB→GLSL output buffer overflow (`appendString`)
- `arbhelper.c` `appendString(str, strLen)` reserves `strLen` bytes and then `strcpy`s the whole C string. Callers
  pass shorter lengths: `APPEND_OUTPUT(&"0123456789"[i], 1)` (generateVariablePre, the declaration of every PARAM
  array with ≤ 10 entries) copies up to 10 bytes, and nothing keeps room for the NUL. Heap write past the output
  buffer when it happens at the buffer's end (ASan: 75 random program pairs). Also the mechanism behind 2.
  Fix: `memcpy` exactly `strLen`, keep one byte for the terminator.

### 20. Repacked uploads converted with the app's alignment
- After the `GL_UNPACK_ROW_LENGTH`/`SKIP_*` repack (tight rows) every later conversion (`remap_pixel`,
  `pixel_convert`, the GLES upload) still uses `GL_UNPACK_ALIGNMENT`: rows misread, heap over-read past the copy
  (ASan, ~350 sub-image tests). Fix: repack into rows padded to that alignment (with 11).

### 1. Immediate-mode draws pick up the next ARB program / program parameters
- gl4es merges consecutive `glBegin`/`glEnd` blocks (`LIBGL_BEGINEND=1`, default) and draws them later;
  `glBindProgramARB`, `glProgramLocalParameter4*ARB`, `glProgramEnvParameter4*ARB` (and the EXT array forms) do
  not flush the pending block. A quad drawn with program A renders with program B, or with parameters set after
  it. Scissor changes and glEnable do flush, so simple tests miss it.
- Fix: `FLUSH_BEGINEND;` at the top of those 11 functions (as other state setters do). Workaround without a
  rebuild: `LIBGL_BEGINEND=0` in the launcher.
- Ports: Unity 4 immediate mode (GL.Begin/End: Graphics.Blit, GUI, many image effects) with a material change
  between draws. Not yet seen in the two replays (they use vertex arrays for most drawing).

### 7b. `OPTION ARB_fragment_program_shadow` rejected ("Unknown option")
- 42 programs in the corpus (all games: Unity's shadow-receiving shaders) fail to load. With finding 5, drawing
  with one of them crashes. Whether the ports reach them depends on Unity enabling shadows (needs depth textures);
  a device check is needed. Fix would be to accept the option and map `SHADOW2D` targets to `shadow2D`.

### 5. Draw with a rejected / never loaded ARB program enabled crashes
- GL: INVALID_OPERATION, nothing drawn. gl4es: `fpe_CustomVertexShader(initial=NULL)` ->
  `gl4es_getline_for(NULL)` -> SIGSEGV. Turns any parser gap (7, 7b) into a crash.

### 12. ARB vertex program + fixed-function fragment stage draws nothing
- The vertex shader gl4es builds for this combination declares `attribute vec4 _gl4es_Color` twice (once from
  the ARB translation, once from the fixed-function glue): compile error, link failure, the draw does nothing.
  Same cause breaks `OPTION ARB_position_invariant`. Every `arb/vp` test shows it in its fixed-function half.

### 11. `GL_UNPACK_ROW_LENGTH` rows ignore `GL_UNPACK_ALIGNMENT`
- `texture.c` (TexImage2D and TexSubImage2D): stride = `row_length * pixelSize`, never rounded up to the
  alignment. RGB / luminance / odd row lengths (and alignment 8) come out sheared. Fix: round `imgWidth` up.

### 2. ARB `SIN` produces truncated GLSL
- `arbgenerator.c:875` `APPEND_OUTPUT("sin(", 9)`: 9 bytes of a 4-byte literal; the NUL ends the shader. Found
  by `static/literal_lengths.py` (only mismatch in gl4es). Still in master. Fix: length 4.

### 3. Heap overflow on `state.light[n].<property>`
- `arbparser.c:810` allocates `mtxNameLen + strlen(sln) + 8` for `"gl_LightSource[%s].%s"` (needs +18; and
  `mtxNameLen` is 9 for the 13-letter "spotDirection"). FORTIFY aborts; otherwise silent heap corruption.

### 4. `result.color.front` / `result.color.back` alone crash
- `arbparser.c` ~500/527: after pushing gl_FrontColor/gl_BackColor the code still pops the next token (NULL) and
  `strcmp`s it. Fix: `return 0;` as the plain `result.color` case does.

### 6 / 17. Per-vertex data inside glBegin/glEnd that gl4es drops
- `gl4es.c` glMultiTexCoord* (3 variants): "called between glBegin / glEnd but Texture is not active ... ignore the
  call". Wrong whenever a program reads the coordinate: ARB fragment programs on the fixed-function vertex stage
  (`arb/binding/fp/*` vs `fp-texenable/*`), GLSL (`gl_MultiTexCoord0` from glMultiTexCoord stays constant;
  `glTexCoord*` already had a GLSL exception), ARB vertex programs. Fix: record when a GLSL program or an ARB
  program is active.
- Generic attributes (`glVertexAttrib4f(6, ...)` between glBegin and glEnd) are not recorded per vertex at all
  (GLSL `attribute` stays at one value). Not fixed.
- Mesa, for its part, skips the texture matrix for units a fragment program reads but does not sample, so the
  bed keeps texture matrices identity in fragment-program scenes.

### 21. Draw-path crashes
- `glPolygonMode(GL_LINE)` (emulated on the CPU) reads one vertex past the client or VBO arrays
  (`copy_gl_array_texcoord`, array.c:101; 25 fuzz seeds, ASan), and with `GL_POLYGON` + VBO elements it segfaults
  in `fpe_glDrawElements`.
- `glMultiDrawArrays` with a zero count in the list dereferences a NULL renderlist (drawing.c:837).

### 13. Flat shading only works for triangle lists
- `glShadeModel(GL_FLAT)`: quads, quad strips, triangle strips, fans and polygons come out smooth-shaded.
- (Not a bug: smooth quad strips split along the other diagonal than Mesa; GL allows either.)

### 14. ARB vertex program translation errors (GLSL compile fails, program unusable)
- `vertex.fogcoord` becomes the float `gl_FogCoord`, so any swizzle fails (ARB defines it as (f, 0, 0, 1)).
- `MOV result.pointsize, x` becomes an assignment to `vec4(_gl4es_Point.size, ...)`; `result.fogcoord` gets a vec4.
- `OPTION ARB_position_invariant`: see 12.

### 7. Valid ARB syntax rejected (program error)
- scalar `PARAM` with an integer (`PARAM k = 1;`) or negative value (`PARAM k = -0.5;`); inline vector constants
  in instructions (`ADD r, a, {0.5, 0, 0, 1};`); `SWZ` with 0/1/negated components; `texture` without an index;
  `RECT` targets; relative addressing without offset (`c[a.x]`); matrix row ranges
  (`state.matrix.texture[0].row[1..2]`); in fragment programs `state.depth.range`, `state.fog.color`,
  `state.fog.params`, `state.texenv[n].color`; in vertex programs `state.clip[n].plane`, `state.point.*`,
  `state.texgen[n].*`, `state.matrix.texture[n].row[..]`, `state.fog.params`.
- None of these in the Unity corpus (it uses braced scalars `{ 0 }` in PARAM arrays, CUBE, TXP, KIL, generic
  attributes, `OPTION ARB_precision_hint_fastest`: all accepted).

### 15. Client array state queries answer 0
- apitrace reads client vertex arrays back with `glGetIntegerv(GL_VERTEX_ARRAY_SIZE / _TYPE / _STRIDE)` at draw
  time; through gl4es the recording has `glVertexPointer(size = 0, type = 0)`. Only matters for tools (and apps
  that save/restore array state through glGet); the bed records traces on Mesa because of it.

### 16. Pixel transfer coverage
- Uploads from BYTE, SHORT, INT, UNSIGNED_INT, FLOAT, HALF_FLOAT sources and RED/GREEN/BLUE formats come out
  wrong; many glReadPixels / glGetTexImage format-type pairs too (see `results-*.json`, groups `tex_upload`,
  `readpixels`, `gettex`). Not used by the Unity ports. 16-bit internal formats (ALPHA12, INTENSITY16,
  LUMINANCE16_ALPHA16) with `GL_UNSIGNED_SHORT` data are still wrong after the pixel.c:159 fix.

### Piglit (quick_gl, `results/piglit-ship.md`)
- Through `ship`: 821 tests pass on Mesa and fail through gl4es, 55 crash. Much of it is GL 3.x material gl4es
  does not claim (GLSL 1.50, transform feedback, multisample) or the test environment (EGL_BAD_MATCH: visuals
  gl4es's GLX cannot give on llvmpipe).
- Crashes worth a look upstream: GL 2.0 shader API on error/edge paths (`glGetUniform*`, `glBindAttribLocation`
  scratch names, `glGetAttribLocation`, `glGetActiveUniform` inside glBegin, `glDeleteShader` twice, GLSL
  recursion and unresolved-function link errors, `glsl-novertexdata`), `vp-bad-program` (= 5),
  `gl-1.4-dlist-multidrawarrays` and `draw-elements`/`draw-vertices` (1.5), `arb_fragment_program@recompile`,
  multi-threaded GLX (gl4es is not thread-safe across contexts).
- Other: S3TC error handling, `GL_MAX_PROGRAM_PARAMETERS_ARB` 64 for vertex programs (spec minimum 96),
  texture LOD/level clamps, `fp-long-alu` rejected.
- `fix`: same picture (822/52); the candidate fixes target paths piglit hardly exercises. The 4 tests that
  looked worse fail on both builds when run alone (load flakiness).

### Real games (apitrace, `results/replay-*.md`)
- Recorded on Mesa told to be GL 2.1 (`trace/trace_unity.sh`, `trace/ext_override.sh`), replayed on Mesa and on
  gl4es, every 4th frame compared.
- Ittle Dew (584 frames: title, intro, new game, walking, menus): matches except a slightly softer HUD icon
  (DXT 16-bit), same with `fix`.
- Teslagrad (192 frames: menus with the point-light highlight, new game): `ship` **aborts at startup**
  (finding 10); `fix` replays all of it and matches Mesa (0 of 48 sampled frames differ).

### Minor / not bugs
- DXT textures are decoded to 16-bit colour: slightly softer icons in the Ittle Dew replay (max diff 71 on one
  HUD button). Known trade-off.
- a744af14 prints generated shaders to stdout (`fpe_CustomVertexShader(...)`): the log spam Westonpack's binary
  NOPs.
- The Ittle Dew replay (584 frames: title, new game, walking, menus) otherwise matches Mesa frame for frame.

## Next steps (open)
- **Device check** before anything ships: the ports carry Westonpack's aarch64 binary with byte patches. Either
  build a744af14 + `ship-fixes.patch` + `candidate-fixes.patch` for aarch64 in the bullseye chroot and compare it
  with the shipped one on the Cube XX (Ittle Dew, Teslagrad: menus, shadows if any, quit), or byte-patch the
  shipped binary for finding 10 alone (the one the Teslagrad stream proves). Take the Cube lock first.
- `LIBGL_BEGINEND=0` in the Unity 4 launchers is a no-rebuild workaround for finding 1; cost unmeasured.
- Upstream (ptitSeb/gl4es): 10, 18, 19, 2, 3, 4, 1, 11/20 are small, self-contained fixes with repros here; the
  user decides what gets filed.
- Test-bed gaps: every `arb/vp` test also draws the vertex program through the fixed-function fragment stage,
  so finding 12 fails them all and hides other vertex-program differences (split it into its own picture);
  ARB shadow programs need depth textures in the scene once 7b is fixed; `aarch64/run_shipped.sh` unfinished.

## Device check 2026-10-05 (RG Cube XX, Knulli, 192.168.1.61)

Arm64 gl4es rebuilt from source as a drop-in for the ports' Westonpack binary (`aarch64/build_aarch64.sh`:
a744af14, NOX11 + NOEGL + NO_INIT_CONSTRUCTOR, `aarch64/glxpass.c` forwarding glX* to glxsdl's crusty_glX*, the
ports' fixes incl. Teslagrad's packed depth-stencil main FBO, GCC 10 in the bullseye chroot; exports identical to
the shipped binary). Two builds: `ship` (c3ee348a, the ports' fixes) and `fix` (26b19515, + candidate-fixes.patch).
A/B with `aarch64/cube_ab_pc.sh` (launch from the installed launcher, 30-45 s to the menu/title, highlight moved,
framebuffer shot, fps, memory, log errors), results and shots in `results/cube/`:

| game | gl4es | picture | fps (last 6 s) | RSS | errors |
|--|--|--|--|--|--|
| Teslagrad | installed 3730deab | letterbox menu, highlight | 38-56 (menu settling) | 428 MB | none |
| Teslagrad | ship c3ee348a | same | 42-57 | 429 MB | none |
| Teslagrad | fix 26b19515 | same | 44-54 | 430 MB | none |
| Ittle Dew | installed 4a4e5dd4 | title | 53-60 | 302 MB | none |
| Ittle Dew | ship c3ee348a | title | 56-60 | 302 MB | none |
| Ittle Dew | fix 26b19515 | title, identical to ship | 57-60 | 303 MB | none |

So the source build works on the device, and the candidate fixes cost nothing visible. Not tested yet: long play
(the heap overflow's random-crash effect needs hours, or the quit-time SIGSEGV count over many launches), RG351P /
RG353V, other GPUs. Both ports were left with their own gl4es; nothing installed.
