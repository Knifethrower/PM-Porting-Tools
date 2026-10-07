# unityport

First-run conversion of Unity 5.x–2019 **Linux (Mono)** games for GLES-only ARM handhelds, as
used by the Night in the Woods and Gone Home ports, in one reusable tool. A new Unity port gets a
`game.toml` instead of a copied and edited `install.py`.

- One engine: the steps run in order, each at most once; every rewritten file goes through
  `<file>.tmp` and a commit recorded in the state file, so an interrupted setup (patcher killed,
  battery flat) resumes where it stopped and never leaves a half-written file.
- A step library: `shaders`, `settings`, `textures`, `sprites`, `audio`, `fmod`, `timing`.
- `survey`: reads a game folder on the PC and reports what a port needs, with a starting config.

Unity 4 games (serialized file v9, gl4es) are a different pipeline: `converters/unity4/` and the
Ittle Dew / Usagi ports.

## Use

On the PC:

```
PYTHONPATH=<this repo>/unity/unityport python -m unityport survey "<game folder>"
PYTHONPATH=<this repo>/unity/unityport python -m unityport install game.toml "<copy of the game folder>"
```

On the device, from the port's `patchscript`, with PortMaster's `python_3.11` runtime mounted and
a folder with UnityPy (built with ASTC support) and its dependencies for aarch64 as the package folder:

```
export PYTHONPATH="$GAMEDIR/tools/pylib:$GAMEDIR/tools"
"$PYDIR/bin/python3" -m unityport install "$GAMEDIR/tools/game.toml" "$GAMEDIR/gamedata"
```

Ship `unityport/` (this package folder, about 100 KB with the ADPCM encoders) in the port's
`tools/`, next to `game.toml`. The launcher re-runs setup when `gamedata/.installed` differs from
`game.installed_version`, so bumping that number (after adding a step) reaches existing installs;
only the new steps run.

Needs: UnityPy 1.25, astc_encoder, Pillow, lz4, texture2ddecoder (all in
`pylib/unitypy-astc-full`); `tomllib` (Python 3.11; on older PC Pythons `pip install tomli`).

## Steps

| Step | What | Where it came from |
|--|--|--|
| `shaders` | Adds a GLSL ES 3.00 variant to every OpenGLCore shader program (`gles_shaders.py`) | NITW, Gone Home |
| `settings` | `m_GraphicsAPIs` (GLES3 first), AudioManager sample rate, `textureQuality` of every quality level | both |
| `textures` | DXT/BC7/RGB(A) → ASTC, scaled or sharp by rules, new mips; same-file sprites rescaled | NITW (2D), Gone Home (3D) |
| `sprites` | Sprites whose texture is in another file, rescaled from the recorded sizes | both |
| `audio` | Long in-RAM clips → Streaming; PCM16 → IMA ADPCM in the FSB5 containers | Gone Home |
| `fmod` | FMOD Studio integration mixes at 44100 Hz (FMOD 1.09 + dmix crash) | NITW |
| `timing` | TimeManager: maximum allowed timestep and fixed timestep (game time vs FPS) | Gone Home |

## Config reference (`game.toml`)

Anything left out keeps the default from `unityport/config.py`. Unknown keys are an error.

```toml
steps = ["shaders", "settings", "textures", "sprites", "fmod"]

[game]
name = "Night in the Woods"        # for messages
exe = "NITW.x86_64"                # names after setup (required)
data = "NITW_Data"                 # (required)
original_exe = "Night in the Woods.x86_64"   # renamed to exe if present (space-free names)
original_data = "Night in the Woods_Data"
unity_version = "5.6.2p4"          # only a warning when different
gog_prefix = "data/noarch/game/"   # where the game sits inside GOG's .sh (a zip)
gog_skip_x86 = true                # leave 32-bit files out when unpacking
drop = ["{exe_base}.x86", "{data}/Mono/x86", "{data}/Plugins/x86"]   # deleted after layout
missing_message = "..."            # when neither the game nor a .sh is in the folder
state_file = ".unityport.json"     # in the data folder
installed_version = 1              # written to <gamedir>/.installed at the end


[shaders]
files = ["*.assets", "Resources/*"]

[settings]
graphics_apis = [11, 17]           # 11 GLES3, 17 GLCore; [] = keep
audio_sample_rate = 44100          # 0 = keep
texture_quality = 0                # omit to keep

[textures]
files = ["sharedassets*.assets", "resources.assets"]
sort = "number"                    # "name" (default) or "number"
scale = 0.5
min_side = 32                      # never scale a side below this
keep_max = 128                     # never scale textures whose longest side is at most this
min_convert = 8                    # leave smaller textures alone
block = [6, 6]                     # ASTC block of scalable textures
sharp_block = [4, 4]               # ASTC block of sharp textures
sharp_block_dxt1 = [5, 5]          # sharp DXT1 sources (omit = sharp_block)
sharp_names = 'dialogue_|text(?!ure)|letter|font|glyph'   # case-insensitive regex
scale_files = "sharedassets*"      # only these files' textures may be scaled (omit = all)
scale_mipmapped_only = false       # only mipmapped textures may be scaled
mipmaps = "none"                   # "keep" (full new chain where there was one) or "none"
quality = "fast"                   # astc-encoder preset: fastest, fast, medium, thorough

[audio]
files = ["sharedassets*.assets", "resources.assets"]
stream_min_mb = 8
adpcm = true                       # AUDIO_ADPCM=0/1 in the environment overrides

[fmod]
file = "resources.assets"
sample_rate = 44100

[timing]
fixed_timestep = 0.0333333         # 0 = keep
max_timestep = 0.2                 # 0 = keep
```

Choosing the texture rules: run `survey` and look at the largest textures and their names. Text,
fonts and UI atlases whose sprites are stored in pixels (NGUI) must stay sharp; world textures of
a 3D game can lose half their size with little visible difference on a 640x480 screen.

## Verification

`tests/compare_gonehome.sh <work dir>` runs Gone Home's own `install.py` and
`unityport install examples/gonehome.toml` on two fresh copies of the GOG installer and compares
the resulting game folders (state files excluded): they must be identical. See `CHANGES.md`
for the results.

`tests/compare_nitw.sh` does the same for Night in the Woods with `examples/nitw.toml`:
identical as well.
