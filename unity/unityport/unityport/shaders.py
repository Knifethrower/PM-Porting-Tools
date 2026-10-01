"""Step shaders: add a GLSL ES 3.00 variant to every OpenGL-core shader program (gles_shaders.py)."""
import gc, os
from . import gles_shaders as GS


def run(inst):
    import UnityPy
    c = inst.cfg['shaders']
    files = [f for f in inst.files(c['files']) if os.path.splitext(f)[1].lower() not in c['skip_ext']]
    total = 0
    for i, f in enumerate(files, 1):
        if inst.file_done('shaders', f):
            continue
        try:
            env = UnityPy.load(f)
        except Exception:
            continue
        changed = 0
        for o in env.objects:
            if o.type.name != 'Shader':
                continue
            t = o.read_typetree()
            if GS.patch_shader_tree(t):
                o.save_typetree(t); changed += 1
        o = t = None                           # drop references so the file is released
        if changed:
            data = list(env.files.values())[0].save()
            del env; gc.collect()
            inst.write_tmp(f, data)
            inst.commit('shaders', f)
            total += changed
            inst.log(f'shaders: {os.path.basename(f)}: {changed} ({i}/{len(files)} files)')
        else:
            del env; gc.collect()
            inst.skip_file('shaders', f)
    inst.log(f'shaders: {total} converted')
