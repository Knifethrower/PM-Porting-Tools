# gl4es test bed

Finds gl4es bugs before a device does. The same GL work runs on **Mesa desktop GL (llvmpipe)**, the reference,
and **through gl4es** (x86_64 build on Mesa llvmpipe GLES 2, with the Mali capability profile so gl4es takes the
code paths it takes on the handhelds). Anything that differs is a gl4es bug or a gl4es limitation;
`FINDINGS.md` lists what was found and what each means for the ports.

Everything runs in WSL (Ubuntu 24.04) under Xvfb, never on the desktop. Work dir: `~/gl4es-test` (`$GT`).

## Layers

| Layer | What it tests | Run |
|--|--|--|
| `gt/` own differential tests (C, ~13,100 cases) | texture upload for every format/type/internal format and unpack state, glReadPixels / glGetTexImage for every format/type and pack state (plus writes outside the image), S3TC, texture parameters and mipmaps, copies; ARB programs: syntax probes, each state binding, program lifecycle (the id-recycling crash), invalid-program draws, random fragment/vertex/paired programs; fixed-function fuzz (texenv/combine, lighting, fog, blending, depth/stencil, texgen ... each seed logs its features); every primitive through every submission path; glGet / push-pop / matrix / display list / raster state; FBOs the Unity way; desktop GLSL that gl4es rewrites | `gtrun.py` |
| Unity shader corpus | every ARB program the Unity 4 games ship (`extract_arb.py`), through `gt` | `gtrun.py -c ~/gl4es-test/corpus -m corpus/` |
| piglit | Mesa's GL conformance suite: tests that pass on Mesa and not through gl4es | `run_piglit.sh` |
| apitrace replays | the exact call stream a Unity 4 player sends to gl4es (recorded through gl4es), replayed on Mesa and on gl4es, frame by frame | `trace/trace_unity.sh`, `trace/replay_compare.sh` |
| AddressSanitizer | memory errors that never show as wrong pixels (the ARB-id bug was one) | variant `ship-asan` in any of the above |
| static checks | bug patterns grep can see (literal/length mismatches found the SIN bug) | `static/literal_lengths.py <gl4es>/src` |

## Setup (once)

```bash
bash setup.sh            # gl4es variants: ship, ship-asan, head, orig  ->  ~/gl4es-test/lib/<variant>/libGL.so.1
make -C gt               # ~/gl4es-test/bin/gt
bash setup_piglit.sh     # piglit with GLX (~15 min; package list inside)
python3 extract_arb.py ~/gl4es-test/corpus ittledew ~/ittledew/gog_orig   # one call per game
```
apt (as root): `piglit apitrace waffle-utils libwaffle-dev xvfb xdotool openbox python3-numpy python3-pil`.

Variants: `ship` = gl4es a744af14 (Westonpack's v1.1.7, what the ports ship) + the ports' two fixes;
`ship-asan` = the same with AddressSanitizer/UBSan; `head` = upstream master + the same fixes; `orig` =
a744af14 without fixes (must fail `tex_upload/*/ALPHA/UNSIGNED_SHORT` and `arb/lifecycle`: proof the bed sees
known bugs). Settings ride along after `@`: `ship@LIBGL_BEGINEND=0`.

## Running

```bash
python3 gtrun.py -v ship,fix -c ~/gl4es-test/corpus -j 6   # everything + the Unity corpus, ~15 min per variant
python3 gtrun.py -v ship -m arb/fp/ -o ~/gl4es-test/results/x    # a group
python3 gtrun.py -v ship -c ~/gl4es-test/corpus -m corpus/       # the real shaders
bash run_piglit.sh ship quick_gl 6                             # hours
bash trace/trace_unity.sh ittledew idw "k:z w:2 k:z w:2 ..."  # record (START= seconds before the keys)
bash trace/replay_compare.sh ~/gl4es-test/traces/idw.trace ship 5
```
`gtrun.py` writes `report.md` (per group pass/fail/crash/sanitizer, fuzz feature attribution, every failure with
its reason), `results-<variant>.json` and `diff/<variant>/*.png` (reference | gl4es | differing pixels in
magenta). A test that crashes is isolated and the rest of its batch continues. `gt -l` lists tests,
`gt -o DIR NAME...` runs some, `gt -p fp|vp SEED` prints a generated ARB program.

Analysis helpers (all take a `results-<variant>.json` or result folder):
`summarize.py` (status per group + the most common failure reasons), `attrib.py` (fuzz: which single features the
failing seeds share), `asan_summary.py <dir> <variant>` (AddressSanitizer/UBSan reports grouped by kind and
gl4es frame), `gtrun.py --compare-only` (re-score stored outputs after a tolerance change, runs nothing).

Arm64 drop-in for the ports: `wsl -u root -e bash aarch64/build_aarch64.sh ship fix` builds the ports' gl4es from
source (same exports as Westonpack's glxpass binary; FINDINGS "Device check"), `aarch64/cube_ab_pc.sh` A/B-tests a
build on the RG Cube XX (take the Cube lock first).

Experimental: `aarch64/run_shipped.sh <port gamedir> <out> [tests]` runs gt against a port's own aarch64
gl4es + glxsdl under qemu with the arm64 chroot's Mesa. It still segfaults inside SDL/glxsdl at the first
context (unfinished); the device is the reliable check of the shipped binary.

Reading a failure: run the single test with `LIBGL_LOGSHADERERROR=1` (the GLSL gl4es generated and the
compiler's message) or under gdb (`env $(gl4es_env ship) gdb --args ~/gl4es-test/bin/gt -o /tmp/x NAME`, after
`. env.sh` and an Xvfb).

## Known limits of the method
- Mesa is the reference, so Mesa's own quirks look like gl4es bugs. Seen: Mesa skips the texture matrix for a
  unit a fragment program reads but does not sample (the gt fragment-program scenes keep it identity).
- llvmpipe GLES 2 is not the Mali blob: driver-specific behaviour (precision, BGRA handling, readback stalls) is
  out of scope; a finding still needs a device check before it is called a device bug.
- The aarch64 build the ports ship is the same source; compiler-specific problems would need the replay
  under qemu or on a device.
- Tolerances: 2-3/255 per channel (more for 4/5-bit formats), fuzzers allow a few % of edge pixels.
