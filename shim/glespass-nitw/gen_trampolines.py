"""Generate glespass trampolines: one exported symbol per GL/GLX entry point that gl4es's
libGL.so.1 exports, so box64's libGL wrapper finds every function it looks up.

  gl*  -> jumps to the system GLES/EGL implementation (resolved once at load), or a no-op
          returning 0 when GLES has no such function (desktop-only GL).
  glX* -> jumps to crusty's GLX implementation (glXGetProcAddress -> glXGetProcAddressARB).

Writes build/trampolines.S and build/symbols.h.
"""
import os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
names = [n.strip() for n in open(os.path.join(here, 'gl4es_exports.txt'))]
names = sorted({n for n in names if re.fullmatch(r'gl[A-Z]\w*|glX\w+', n)})
gl = [n for n in names if not n.startswith('glX')]
glx = [n for n in names if n.startswith('glX')]
if 'glXGetProcAddress' not in glx:
    glx.append('glXGetProcAddress')
allnames = gl + glx
os.makedirs(os.path.join(here, 'build'), exist_ok=True)

with open(os.path.join(here, 'build', 'trampolines.S'), 'w', newline='\n') as f:
    f.write('    .text\n')
    for i, n in enumerate(allnames):
        f.write(f'''    .globl {n}
    .type {n}, %function
    .p2align 2
{n}:
    adrp x16, glespass_ptrs + {i * 8}
    ldr x16, [x16, :lo12:glespass_ptrs + {i * 8}]
    br x16
    .size {n}, .-{n}
''')
    f.write('    .section .note.GNU-stack,"",%progbits\n')

with open(os.path.join(here, 'build', 'symbols.h'), 'w', newline='\n') as f:
    f.write(f'#define GLESPASS_NUM_GL {len(gl)}\n#define GLESPASS_NUM_ALL {len(allnames)}\n')
    f.write('static const char *const glespass_names[GLESPASS_NUM_ALL] = {\n')
    f.write(''.join(f'    "{n}",\n' for n in allnames))
    f.write('};\n')
print(f'{len(gl)} gl + {len(glx)} glX trampolines')
