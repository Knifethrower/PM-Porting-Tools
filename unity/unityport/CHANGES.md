# unityport: history and verification

## 1.0 (2026-09-30)

Made from the Gone Home installer (the port's `release/gonehome/tools`, the most complete
one: setup versioning, textures with mips, audio, timing) and the Night in the Woods installer
(`nightinthewoods/tools`: renames, 2D texture rules, FMOD Studio sample rate). The per-game
constants became config keys; the engine and the step code are unchanged apart from that.

Verification on the PC (Windows, Python 3.10, UnityPy 1.25.3):

- `tests/compare_gonehome.sh`: Gone Home's own `install.py` and `unityport install
  examples/gonehome.toml`, each on a fresh copy of the GOG installer
  (`gone_home_2020_01_28_35744.sh`, AUDIO_ADPCM=1): **identical game folders** (181 files,
  1.2 GB; state files excluded), and both identical to the conversion shipped for the RG353V test. About 2.5 minutes each.
- `survey` on the original Gone Home files: 6 s; reports Unity 2018.4.9f1, Mono, GLCore/Vulkan,
  99 shaders without GLES3 programs, 1,532 textures (DXT5/DXT1/RGBA32), 365 PCM clips, the
  0.033 s maximum timestep; the suggested steps are exactly the ones the port uses, and the
  printed config parses.
- `tests/compare_nitw.sh`: the Night in the Woods port's install.py and
  `unityport install examples/nitw.toml` on two fresh copies of the GOG installer
  (`night_in_the_woods_en_406_21109.sh`): **identical game folders** (614 files, 2.0 GB; state
  files excluded). About 10 minutes each (19,961 textures, 17,716 sprites rescaled).

Not verified yet:

- On the device (python_3.11 runtime + `pylib/unitypy-astc-full`): the code is the same as the
  Gone Home installer that ran there, but the package layout (`python3 -m unityport`) has not
  been run on a device.
