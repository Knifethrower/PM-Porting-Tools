"""Step fmod: the FMOD Studio Unity integration's default output rate (FMODStudioSettings).

Only for games that use FMOD Studio (Plugins/libfmodstudio*.so). FMOD 1.09 cannot handle the
1114-frame periods ALSA's plug layer hands it when it mixes at 48 kHz on a 44.1 kHz dmix
(dArkOS); mixing at 44100 gives it the 1024-frame periods it asks for.
"""
import gc, os
from . import fmod_samplerate as FS


def run(inst):
    c = inst.cfg['fmod']
    path = os.path.join(inst.data, c['file'])
    try:
        data = FS.patched_bytes(path, c['sample_rate'])
    except SystemExit as e:
        inst.log(f'fmod: sample rate not changed ({e})')
        return
    gc.collect()
    inst.write_tmp(path, data)
    inst.commit('fmod', path)
    inst.log(f'fmod: FMOD mixes at {c["sample_rate"]} Hz')
