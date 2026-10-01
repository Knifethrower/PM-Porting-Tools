"""Survey a Unity game folder: what a PortMaster port of it needs, and a starting config.

Reads only; prints a report and a TOML skeleton for `unityport install`. Runs on the PC.
"""
import collections, glob, os, struct

API = {0: 'OpenGL2', 2: 'D3D9', 4: 'D3D11', 8: 'OpenGLES2', 11: 'OpenGLES3', 16: 'Metal',
       17: 'OpenGLCore', 18: 'D3D12', 21: 'Vulkan'}
SHADER_PLATFORM = {0: 'GL', 4: 'D3D11', 5: 'GLES20', 9: 'GLES3x', 14: 'Metal', 15: 'GLCore',
                   18: 'Vulkan'}
LOAD = {0: 'DecompressOnLoad', 1: 'CompressedInMemory', 2: 'Streaming'}
COMP = {0: 'PCM', 1: 'Vorbis', 2: 'ADPCM', 3: 'MP3'}


def mb(n):
    return f'{n / 1e6:,.1f} MB'


def find_data(gamedir, data):
    if data:
        return os.path.join(gamedir, data)
    for d in sorted(glob.glob(os.path.join(gamedir, '*_Data'))):
        if os.path.exists(os.path.join(d, 'globalgamemanagers')) or os.path.exists(os.path.join(d, 'mainData')):
            return d
    raise SystemExit(f'no *_Data folder with globalgamemanagers in {gamedir}')


def elf_arch(path):
    try:
        with open(path, 'rb') as f:
            h = f.read(20)
    except OSError:
        return None
    if h[:4] != b'\x7fELF':
        return 'not ELF'
    return {62: 'x86_64', 3: 'x86', 183: 'aarch64', 40: 'arm'}.get(struct.unpack_from('<H', h, 18)[0], '?')


def run(gamedir, data=None):
    import UnityPy
    from UnityPy.enums import TextureFormat as TF
    d = find_data(gamedir, data)
    base = os.path.basename(d)[:-5]
    print(f'== {base} ({d})')
    notes, steps = [], []

    # ---- player and scripting
    exes = [p for p in glob.glob(os.path.join(gamedir, base + '*')) if os.path.isfile(p)]
    for p in exes:
        print(f'executable: {os.path.basename(p)} ({elf_arch(p)})')
    if not any(p.endswith('.x86_64') for p in exes):
        notes.append('no Linux x86_64 player: Windows-only game, see knowledge section 13.1 (donor Linux player)')
    if os.path.exists(os.path.join(gamedir, 'UnityPlayer.so')):
        print('UnityPlayer.so: yes (Unity 2017.2+ split player)')
    il2cpp = os.path.exists(os.path.join(gamedir, 'GameAssembly.so')) or os.path.isdir(os.path.join(d, 'il2cpp_data'))
    print('scripting: ' + ('IL2CPP (no managed DLL patching possible)' if il2cpp else 'Mono'))
    plugins = sorted({os.path.basename(p) for p in glob.glob(os.path.join(d, 'Plugins', '**', '*.so'), recursive=True)})
    if plugins:
        print('native plugins: ' + ', '.join(plugins))
    if any('fmodstudio' in p.lower() for p in plugins):
        steps.append('fmod')
        notes.append('FMOD Studio integration: add the fmod step (44100 Hz mixing, knowledge section 12)')
    if any('steam' in p.lower() for p in plugins):
        notes.append('Steamworks plugin: may need a stub (patching/steamstub)')
    if any('wwise' in p.lower() or 'akso' in p.lower() for p in plugins):
        notes.append('Wwise audio: not handled by unityport')

    # ---- globalgamemanagers
    ggm = os.path.join(d, 'globalgamemanagers')
    env = UnityPy.load(ggm)
    sf = list(env.files.values())[0]
    version = sf.unity_version
    print(f'Unity version: {version}')
    for o in env.objects:
        n = o.type.name
        if n == 'BuildSettings':
            t = o.read_typetree()
            apis = t.get('m_GraphicsAPIs', [])
            print('graphics APIs: ' + ', '.join(API.get(a, str(a)) for a in apis))
            if 11 not in apis:
                steps.append('settings')
        elif n == 'TimeManager':
            t = o.read_typetree()
            ft, mx = t['Fixed Timestep'], t['Maximum Allowed Timestep']
            print(f'TimeManager: fixed timestep {ft:.4f} s ({1 / ft:.0f} Hz), maximum allowed timestep {mx:.4f} s')
            if mx < 0.1:
                steps.append('timing')
                notes.append(f'maximum timestep {mx:.3f} s: below {1 / mx:.0f} FPS the game runs in slow '
                             'motion (subtitles lag the audio); consider the timing step (knowledge section 12.1)')
        elif n == 'QualitySettings':
            t = o.read_typetree()
            names = [q['name'] for q in t['m_QualitySettings']]
            print(f'quality levels: {", ".join(names)}')
        elif n == 'AudioManager':
            t = o.read_typetree()
            print(f'AudioManager: sample rate {t.get("m_SampleRate") or "default"}, '
                  f'DSP buffer {t.get("m_DSPBufferSize")}')
        elif n == 'PlayerSettings':
            t = o.read_typetree()
            print(f'PlayerSettings: {t.get("companyName")} / {t.get("productName")}, '
                  f'default screen {t.get("defaultScreenWidth")}x{t.get("defaultScreenHeight")}')
    env = sf = None

    # ---- assets: shaders, textures, audio
    files = sorted(glob.glob(os.path.join(d, '*.assets'))) + sorted(glob.glob(os.path.join(d, 'level*'))) + \
        sorted(p for p in glob.glob(os.path.join(d, 'Resources', '*')) if not p.endswith(('.png', '.jpg')))
    shaders, platforms = 0, collections.Counter()
    tex_fmt, tex_n = collections.Counter(), collections.Counter()
    tex_total, tex_mip, big = 0, 0, []
    clips = collections.Counter(); clip_bytes = collections.Counter()
    for f in files:
        try:
            env = UnityPy.load(f)
        except Exception:
            continue
        for o in env.objects:
            n = o.type.name
            if n == 'Shader':
                t = o.read_typetree()
                shaders += 1
                for p in t.get('platforms', []):
                    platforms[SHADER_PLATFORM.get(p, str(p))] += 1
            elif n == 'Texture2D':
                t = o.read_typetree()
                sz = t.get('m_CompleteImageSize', 0)
                try:
                    fm = TF(t['m_TextureFormat']).name
                except ValueError:
                    fm = str(t['m_TextureFormat'])
                tex_fmt[fm] += sz; tex_n[fm] += 1; tex_total += sz
                if t['m_MipCount'] > 1:
                    tex_mip += sz
                big.append((sz, t['m_Width'], t['m_Height'], fm, t['m_MipCount'], t['m_Name'], os.path.basename(f)))
            elif n == 'AudioClip':
                t = o.read_typetree()
                key = (COMP.get(t.get('m_CompressionFormat'), '?'), LOAD.get(t.get('m_LoadType'), '?'))
                clips[key] += 1
                clip_bytes[key] += t['m_Resource']['m_Size']
        env = None
    print(f'shaders: {shaders}; programs per platform: ' +
          ', '.join(f'{k} {v}' for k, v in platforms.most_common()))
    if platforms['GLCore'] > platforms['GLES3x']:
        steps.insert(0, 'shaders')
    print(f'textures: {sum(tex_n.values())}, {mb(tex_total)} ({100 * tex_mip // max(tex_total, 1)}% mipmapped)')
    for k, v in tex_fmt.most_common():
        print(f'  {k:14} {tex_n[k]:5}  {mb(v)}')
    for b in sorted(big, reverse=True)[:8]:
        print(f'  largest: {mb(b[0])} {b[1]}x{b[2]} {b[3]} mips={b[4]} {b[5]!r} ({b[6]})')
    if any(k.startswith(('DXT', 'BC')) for k in tex_fmt):
        steps += ['textures', 'sprites']
        notes.append('DXT/BC textures: the textures step converts them to ASTC; pick sharp_names from '
                     'the texture names (text, fonts, UI atlases) before the first device test')
    print(f'audio clips: {sum(clips.values())}')
    for k, v in clips.most_common():
        print(f'  {k[0]:7} {k[1]:19} {v:5}  {mb(clip_bytes[k])}')
    if any(k[0] == 'PCM' for k in clips) or any(k[1] != 'Streaming' for k in clips):
        steps.append('audio')
    total = sum(os.path.getsize(os.path.join(r, x)) for r, _, fs in os.walk(d) for x in fs)
    print(f'data folder: {mb(total)}')

    print('\n== notes')
    for n in notes or ['nothing unusual found']:
        print('- ' + n)
    order = [s for s in ('shaders', 'settings', 'textures', 'sprites', 'audio', 'fmod', 'timing') if s in steps]
    exe = next((os.path.basename(p) for p in exes if p.endswith('.x86_64')), base + '.x86_64')
    print(f'''
== starting config (edit, then: python3 -m unityport install game.toml <gamedir>)
steps = {order!r}

[game]
name = "{base}"
exe = "{exe}"
data = "{os.path.basename(d)}"
unity_version = "{version}"
installed_version = 1
'''.replace("'", '"'))
