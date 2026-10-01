"""Per-game configuration: a TOML file, merged over the defaults below.

Every key is documented in README.md; examples/ has the Gone Home and Night in the Woods files.
"""
import copy

try:
    import tomllib                     # Python 3.11+ (PortMaster's python_3.11 runtime)
except ImportError:                    # older PC Pythons
    import tomli as tomllib

STEPS = ('shaders', 'settings', 'textures', 'sprites', 'audio', 'fmod', 'timing')

DEFAULTS = {
    'game': {
        'name': 'the game',
        'exe': None,                   # e.g. "GoneHome.x86_64" (required)
        'data': None,                  # e.g. "GoneHome_Data" (required)
        'original_exe': None,          # renamed to exe when found (space-free names)
        'original_data': None,         # renamed to data when found
        'unity_version': None,         # only warns when globalgamemanagers says otherwise
        'gog_prefix': 'data/noarch/game/',
        'gog_skip_x86': True,          # leave 32-bit x86 files out when unpacking
        'drop': ['{exe_base}.x86', '{data}/Mono/x86', '{data}/Plugins/x86'],
        'missing_message': None,       # shown when neither the game nor an installer is found
        'state_file': '.unityport.json',   # in the data folder
        'installed_version': 1,        # written to <gamedir>/.installed when everything is done
    },
    'steps': ['shaders', 'settings', 'textures', 'sprites', 'audio', 'timing'],
    'shaders': {
        'files': ['*.assets', 'Resources/*'],
        'skip_ext': ['.png', '.jpg', '.txt', '.dll', '.so'],
    },
    'settings': {
        'graphics_apis': [11, 17],     # GLES3, then GLCore
        'audio_sample_rate': 44100,    # AudioManager; None = keep
        'texture_quality': None,       # QualitySettings textureQuality on every level; None = keep
    },
    'textures': {
        'files': ['sharedassets*.assets', 'resources.assets'],
        'sort': 'name',                # "name" or "number" (sharedassets2 before sharedassets10)
        'scale': 0.5,
        'min_side': 32,                # never scale a side below this
        'keep_max': 128,               # never scale textures whose longest side is at most this
        'min_convert': 4,              # leave textures smaller than this untouched
        'block': [6, 6],               # ASTC block of scalable textures
        'sharp_block': [4, 4],         # ASTC block of sharp (never scaled) textures
        'sharp_block_dxt1': None,      # ASTC block of sharp DXT1 sources; None = sharp_block
        'sharp_names': 'atlas|font|text(?!ure)|letter',    # regex, case-insensitive
        'scale_files': None,           # glob: only textures in these files can be scaled
        'scale_mipmapped_only': False, # only textures with mipmaps can be scaled
        'mipmaps': 'keep',             # "keep": regenerate a full chain if the original had one;
                                       # "none": base level only (2D games)
        'quality': 'fast',             # astc-encoder preset
    },
    'audio': {
        'files': ['sharedassets*.assets', 'resources.assets'],
        'stream_min_mb': 8,            # clips loaded into RAM with this much data are streamed
        'adpcm': True,                 # PCM16 -> IMA ADPCM; env AUDIO_ADPCM=0/1 overrides
    },
    'fmod': {
        'file': 'resources.assets',    # where the FMODStudioSettings asset lives
        'sample_rate': 44100,
    },
    'timing': {
        'fixed_timestep': 1 / 30,
        'max_timestep': 0.2,
    },
}


def _merge(base, over, path=''):
    for k, v in over.items():
        if k not in base:
            raise SystemExit(f'config: unknown key {path}{k}')
        if isinstance(base[k], dict) and isinstance(v, dict):
            _merge(base[k], v, f'{path}{k}.')
        else:
            base[k] = v


def load(path):
    with open(path, 'rb') as f:
        user = tomllib.load(f)
    cfg = copy.deepcopy(DEFAULTS)
    _merge(cfg, user)
    g = cfg['game']
    if not g['exe'] or not g['data']:
        raise SystemExit('config: game.exe and game.data are required')
    for s in cfg['steps']:
        if s not in STEPS:
            raise SystemExit(f'config: unknown step {s!r} (known: {", ".join(STEPS)})')
    return cfg
