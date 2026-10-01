"""Step sprites: scale sprites whose texture lives in another assets file that the textures
step scaled (the sizes come from the state file, so no copy of the original game is needed)."""
import gc, os
from .textures import scale_sprite


def fix_cross_file_sprites(path, scaled_by_file):
    """Writes <file>.tmp and returns the number of sprites fixed (0 = nothing written)."""
    import UnityPy
    env = UnityPy.load(path)
    sf = list(env.files.values())[0]
    touched = []
    for pid, o in sf.objects.items():
        if o.type.name != 'Sprite':
            continue
        s = o.read_typetree()
        tp = s['m_RD']['texture']
        if not tp['m_FileID']:
            continue
        tfile = os.path.basename(sf.externals[tp['m_FileID'] - 1].path)
        dims = scaled_by_file.get(tfile, {}).get(str(tp['m_PathID']))
        if not dims:
            continue
        ow, oh, nw, nh = dims
        scale_sprite(s, nw / ow, nh / oh)
        touched.append((o, s))
    if not touched:
        return 0
    for o, s in touched:
        o.save_typetree(s)
    with open(path + '.tmp', 'wb') as f:
        f.write(sf.save()); f.flush(); os.fsync(f.fileno())
    return len(touched)


def run(inst):
    c = inst.cfg['textures']
    fixed = 0
    for f in inst.files(c['files'], c['sort']):
        if inst.file_done('sprites', f):
            continue
        n = fix_cross_file_sprites(f, inst.state['scaled'])
        gc.collect()
        if n:
            inst.commit('sprites', f); fixed += n
        else:
            inst.skip_file('sprites', f)
    inst.log(f'sprites: {fixed} cross-file sprites fixed')
