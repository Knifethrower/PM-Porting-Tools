"""Write the gmsprites list for a GameMaker data.win: every sprite_add / background_add call with
a constant .png path, as "<png> <frames> <removeback> <smooth>" (paths relative to the game
folder), and print what it skipped.

usage: sprite_list.py <data.win> <out list>

Verified on bytecode 16 (GMS 1.4.1804, Daydreamer: Awakened Edition). Pattern: the numeric
arguments as pushI + conv, optionally working_directory, push.s "\\Dir\\file.png", add, call.
Paths built at runtime and frame counts in variables are not found (listed as skipped). A file
loaded with differing arguments gets the variant used most; the loader checks every frame (CRC)
and leaves non-matching loads as plain RGBA.
"""
import struct, sys
from collections import Counter, defaultdict

d = open(sys.argv[1], 'rb').read()
chunks = {}; p = 8
while p < len(d):
    n = d[p:p+4].decode(); s = struct.unpack('<I', d[p+4:p+8])[0]; chunks[n] = (p+8, s); p += 8+s
u32 = lambda a: struct.unpack('<I', d[a:a+4])[0]
def cstr(a): return d[a:d.index(b'\0', a)].decode('latin1')

o, s = chunks['STRG']; n = u32(o)
strs = []
for i in range(n):
    a = u32(o+4+4*i); ln = u32(a); strs.append(d[a+4:a+4+ln].decode('utf8', 'replace'))

# function name of each call instruction
calls = {}
o, s = chunks['FUNC']; n = u32(o)
for i in range(n):
    e = o+4+12*i; name = cstr(u32(e)); addr = u32(e+8)
    for k in range(u32(e+4)):
        calls[addr] = name; addr += u32(addr+4) & 0x07FFFFFF

variants = defaultdict(Counter)
o, s = chunks['CODE']
for a in range(o, o+s-16, 4):
    w = u32(a)
    if (w >> 24) != 0xC0 or ((w >> 16) & 0xF) != 6 or u32(a+4) >= len(strs):
        continue
    path = strs[u32(a+4)]
    if not path.lower().endswith('.png') or (u32(a+8) >> 24) != 0x0C:
        continue
    fn = calls.get(a+12)
    if fn not in ('sprite_add', 'background_add'):
        continue
    j = a-8 if (u32(a-8) >> 24) == 0xC3 else a      # working_directory push
    args = []
    for _ in range((u32(a+12) & 0xFFFF) - 1):        # numeric args, nearest first = arg 1
        if u32(j-4) == 0x07520000 and (u32(j-8) >> 24) == 0x84:
            v = u32(j-8) & 0xFFFF; args.append(v - 0x10000 if v & 0x8000 else v); j -= 8
        else:
            args.append(None); break
    rel = path.lstrip('\\').replace('\\', '/')
    if fn == 'background_add':
        variants[rel][(1, 0, 0)] += 1                # background_add never splits; removeback 0 here
        continue
    frames = args[0] if args and args[0] is not None else None
    rb = args[1] if len(args) > 1 and args[1] is not None else 0
    sm = args[2] if len(args) > 2 and args[2] is not None else 0
    if frames is None:
        print('skipped (frame count not constant):', rel); continue
    variants[rel][(max(frames, 1), int(rb > 0), int(sm > 0))] += 1

with open(sys.argv[2], 'w', newline='\n') as f:
    for rel in sorted(variants):
        v = variants[rel]
        if len(v) > 1:
            print('several variants for %s: %s' % (rel, dict(v)))
        frames, rb, sm = v.most_common(1)[0][0]
        f.write('%s %d %d %d\n' % (rel, frames, rb, sm))
print(len(variants), 'images')
