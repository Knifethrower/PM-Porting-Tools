"""Set BuildSettings.m_GraphicsAPIs in a Unity 5.x globalgamemanagers file.
GraphicsDeviceType: 8=OpenGLES2 11=OpenGLES3 17=OpenGLCore 21=Vulkan.
Usage: python set_graphics_apis.py <Data dir> 11 [17 ...]"""
import os, sys, UnityPy
path = os.path.join(sys.argv[1], 'globalgamemanagers')
apis = [int(a) for a in sys.argv[2:]]
env = UnityPy.load(path)
for o in env.objects:
    if o.type.name == 'BuildSettings':
        t = o.read_typetree(); print('before', t['m_GraphicsAPIs'])
        t['m_GraphicsAPIs'] = apis; o.save_typetree(t); print('after ', apis)
data = list(env.files.values())[0].save()
open(path, 'wb').write(data)
