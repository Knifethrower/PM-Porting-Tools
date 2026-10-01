"""Find the game in the game folder, or unpack GOG's Linux installer there; rename to the
configured names; drop the 32-bit x86 files."""
import glob, os, shutil, sys, zipfile
from .engine import log


def run(inst):
    g, c = inst.gamedir, inst.cfg['game']
    exe, data = c['exe'], c['data']
    orig_data = c['original_data']
    have = os.path.isdir(inst.data) or (orig_data and os.path.isdir(os.path.join(g, orig_data)))
    if not have:
        installers = sorted(glob.glob(os.path.join(g, '*.sh')))
        if not installers:
            sys.exit(c['missing_message'] or
                     f'Game files not found. Copy the Linux version of {c["name"]} '
                     f'("{c["original_exe"] or exe}" and "{orig_data or data}"), or the GOG Linux '
                     f'installer (.sh), into the gamedata folder.')
        unpack_gog(inst, installers[0])
    if c['original_exe'] and os.path.exists(os.path.join(g, c['original_exe'])):
        os.replace(os.path.join(g, c['original_exe']), os.path.join(g, exe))
    if orig_data and os.path.isdir(os.path.join(g, orig_data)):
        os.rename(os.path.join(g, orig_data), inst.data)
    if not os.path.exists(os.path.join(g, exe)) or not os.path.isdir(inst.data):
        sys.exit(f'Incomplete game files: need "{c["original_exe"] or exe}" and "{orig_data or data}".')
    exe_base = os.path.splitext(c['original_exe'] or exe)[0]
    for p in c['drop']:
        p = os.path.join(g, p.format(exe_base=exe_base, data=data))
        if os.path.isdir(p):
            shutil.rmtree(p)
        elif os.path.exists(p):
            os.remove(p)
    if c['unity_version']:
        with open(os.path.join(inst.data, 'globalgamemanagers'), 'rb') as f:
            head = f.read(64)
        if c['unity_version'].encode() not in head:
            log(f'warning: this is not the Unity {c["unity_version"]} build the port was made for; '
                'trying anyway')


def unpack_gog(inst, installer):
    """GOG's Linux installer is a shell script with a zip appended; the game is under
    data/noarch/game/."""
    c = inst.cfg['game']
    log(f'unpacking {os.path.basename(installer)}')
    prefix = c['gog_prefix']
    with zipfile.ZipFile(installer) as z:
        members = [m for m in z.infolist() if m.filename.startswith(prefix) and not m.is_dir()
                   and not (c['gog_skip_x86'] and ('/x86/' in m.filename or m.filename.endswith('.x86')))]
        total = sum(m.file_size for m in members) or 1
        free = shutil.disk_usage(inst.gamedir).free
        if free < total + (500 << 20):
            sys.exit(f'Not enough free space to unpack the installer: {free >> 20} MB free, '
                     f'need about {(total >> 20) + 500} MB.')
        done = 0
        for i, m in enumerate(members):
            out = os.path.join(inst.gamedir, m.filename[len(prefix):])
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with z.open(m) as src, open(out + '.part', 'wb') as dst:
                shutil.copyfileobj(src, dst, 1 << 20)
            os.replace(out + '.part', out)
            done += m.file_size
            if i % 50 == 0:
                log(f'  unpacked {done * 100 // total}%')
    os.remove(installer)                       # no longer needed
    log('installer unpacked and removed')
