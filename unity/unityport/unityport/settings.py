"""Step settings (globalgamemanagers): the graphics API list (GLES3 first), the AudioManager
output rate, and optionally the texture quality of every quality level.

GraphicsDeviceType: 8 OpenGLES2, 11 OpenGLES3, 17 OpenGLCore, 21 Vulkan. 44100 Hz matches the
ALSA dmix rate of most handheld firmwares. textureQuality 0 = full size: the low quality levels
drop one or two mip levels of every texture, which blurs text when the textures were already
scaled down by the textures step.
"""
import gc, os


def run(inst):
    import UnityPy
    c = inst.cfg['settings']
    path = os.path.join(inst.data, 'globalgamemanagers')
    env = UnityPy.load(path)
    done = []
    for o in env.objects:
        if o.type.name == 'BuildSettings' and c['graphics_apis']:
            t = o.read_typetree()
            t['m_GraphicsAPIs'] = list(c['graphics_apis'])
            o.save_typetree(t)
            done.append(f'graphics APIs {c["graphics_apis"]}')
        elif o.type.name == 'AudioManager' and c['audio_sample_rate']:
            t = o.read_typetree()
            t['m_SampleRate'] = c['audio_sample_rate']
            o.save_typetree(t)
            done.append(f'audio output at {c["audio_sample_rate"]} Hz')
        elif o.type.name == 'QualitySettings' and c['texture_quality'] is not None:
            t = o.read_typetree()
            for q in t['m_QualitySettings']:
                q['textureQuality'] = c['texture_quality']
            o.save_typetree(t)
            done.append(f'textureQuality {c["texture_quality"]}')
    data = list(env.files.values())[0].save()
    o = t = env = None; gc.collect()
    inst.write_tmp(path, data)
    inst.commit('settings', path)
    inst.log('settings: ' + ', '.join(done))
