# Godot 4 tools

From the Dome Keeper survey (Godot 4.3.1, 2026-10-02). Godot games run on PortMaster's stock
Godot runtimes (the game's `.pck` on a matching engine version); the work is making a pack built
for Steam/Windows run there and fit into a handheld's RAM. Pack format 2 (Godot 4.0-4.3), unencrypted.
Python 3, `zstandard` for the .gdc tools.

| Tool | What |
|--|--|
| `pck_patch.py <pck> res://path=<file>...` | Replace files inside a pack in place: new data appended, the directory entry repointed (offset, size, MD5). Only existing paths. `--classes <stub dir> <abs dir on device>` merges `class_name` stub scripts into the pack's `global_script_class_cache.cfg`; `--list` prints every file. Work on a copy (patch the user's pack on the device at first launch). |
| `pck_assets.py <pck>` | Every imported/exported asset with its imported type and size, biggest first: where the RAM goes (Dome Keeper: 708 MB of 16-bit PCM samples, 313 MB of preloaded music). Also the asset list for the probe. |
| `gdc_tokens.py <pck> <names>` | Decodes Godot 4.3 binary GDScript (`.gdc`, tokenizer v100, zstd; identifiers are UTF-32 XOR 0xb6b6b6b6) and prints what follows each name: which members a stub class must have (`PlayFabServices . login`). |
| `gdc_print.py <pck> <res://x.gdc>` | Prints a `.gdc` as approximate one-line GDScript, for reading. |
| `lazy_music.py` + `LazyAudioStream.gd` | Games that preload their whole soundtrack: repoints each music `.ogg.import` in the pack to an external `.tres` LazyAudioStream that loads the real stream only when a player starts it. No script edits. Dome Keeper title screen: RSS 1.63 to 1.10 GB. |
| `stubs-example/` | GDScript stand-ins for engine singletons/classes a Steam build was compiled with (GodotSteam `Steam`, PlayFab): autoloads for singletons, `class_name` scripts + `pck_patch.py --classes` for classes. Members found with `gdc_tokens.py`. |
| `run_pck.sh` | Runs a pack on a stock Godot binary in a private Xvfb: log, RSS per second, screenshots; optional override.cfg (autoload stubs, `_custom_features`), stubs folder and memory probe. |
| `probe/MemProbe.gd` | Test-only autoload: at 45 s and 100 s of game time (counted from the first frame, so run `run_pck.sh` for 90 s or more) writes Godot's memory monitors and every cached asset from `assets.tsv` with an estimated in-RAM size. `probe/override.example.cfg` shows the autoload wiring. |

Recipe that got Dome Keeper's Steam pack to its title screen on the official `linux_debug` template
with zero script errors: `override.cfg` next to the binary with the Steam feature dropped
(`_custom_features="disable_mods"`) and the singletons as autoload stubs, plus the `class_name`
stubs merged into the class cache. Use the export templates, not the editor binary (it adds the
"editor" feature tag, which changes code paths).

Godot 2/3 games: PortMaster's `frt_2.1.6` did not run even a one-line script on the RG Cube XX
(Knulli), see `SKIPPED.md` (Tanks of Freedom).

## Godot 3 source projects on frt_3.5.2 (2026-10-04)

| Tool | What |
|--|--|
| `export_pck3.sh <project dir> <out.pck>` | Godot 3.5.2 headless (WSL `~/godot352`, official release) export of a Godot 3 project: on a copy, forces GLES2 and ETC1 texture import (frt's GLES2 cannot load the S3TC/ETC2-only VRAM textures of 3D projects), adds a "Linux/X11" preset when missing (at the next free `preset.N`: Godot stops reading at the first gap), imports (the editor's `--quit` never returns: 90 s timeout), `--export-pack`. `PM_CURSOR=1` also adds `pm_cursor.gd` as an autoload. `PM_STRETCH=2d` (or `viewport`) sets stretch mode with aspect keep for fixed-size games. `PM_SET="section:key=value;..."` overrides project settings; `PM_PATCH=<file>` applies a `-p1` patch to the copy (port changes without touching the clone). `PM_FPS=1` (test builds only) adds `pm_fps.gd`, which prints `PMFPS n` to stderr every 2 s (frt ignores `--print-fps`). `PM_INCLUDE` / `PM_EXCLUDE` fill the added preset's filters: games that read `.txt`/`.md`/`.json` at runtime need them (look for the upstream export notes; One More Level crashed in world generation without them). |
| `pm_cursor.gd` + `pm_cursor_draw.gd` | Software mouse pointer (CanvasLayer 128, arrow at the mouse, ~1/20 of the view height). KMSDRM has no hardware cursor, so mouse-driven Godot games need it. |

Launcher: copy the A Key(s) Path port's `A Keys Path.sh` (mounts `frt_3.5.2.squashfs`,
runs `frt_3.5.2 --main-pack`; PortMaster downloads the runtime on first launch). On the RG Cube XX
frt reacted only to gptokeyb2 **keys**, not to the pad: map the pad to the game's keyboard actions.
port.json `runtime`: `["frt_3.5.2"]` (bare key).

3D games on the Mali-G31 (Wind-Up Racer, 2026-10-04): a shadow-casting DirectionalLight cost half the frame rate in GLES2 (31 vs 60 fps); shadow distance, shadow size and render resolution barely mattered. Turn shadows off first (`PM_PATCH`).
