"""Step timing (TimeManager in globalgamemanagers).

Unity advances game time by at most 'Maximum Allowed Timestep' per frame (often 1/30 s), so
below that frame rate the whole game - subtitles, scripted scenes, animation - runs in slow
motion while the audio plays in real time. Raising it keeps game time with real time; a longer
'Fixed Timestep' (physics at 30 Hz instead of 50-60) pays for it, since a slow frame then costs
no more physics steps than before. Check the game's physics feel before shipping a change to
the fixed step.
"""
import gc, os


def run(inst):
    import UnityPy
    c = inst.cfg['timing']
    path = os.path.join(inst.data, 'globalgamemanagers')
    env = UnityPy.load(path)
    for o in env.objects:
        if o.type.name == 'TimeManager':
            t = o.read_typetree()
            if c['fixed_timestep']:
                t['Fixed Timestep'] = c['fixed_timestep']
            if c['max_timestep']:
                t['Maximum Allowed Timestep'] = c['max_timestep']
            o.save_typetree(t)
    data = list(env.files.values())[0].save()
    o = t = env = None; gc.collect()
    inst.write_tmp(path, data)
    inst.commit('timing', path)
    inst.log(f'timing: maximum timestep {c["max_timestep"]} s, fixed timestep {c["fixed_timestep"]:.4f} s')
