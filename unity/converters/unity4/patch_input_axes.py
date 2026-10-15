#!/usr/bin/env python3
"""Unity 4: neutralise the game's own joystick bindings in mainData (the InputManager asset).

A Unity 4 Linux player reads /dev/input/js0 itself on firmwares where that node opens, so with
gptokeyb2 every face button fires twice (game binding + keyboard) and sticks/D-pad move twice or on
the wrong axis. This rebinds every "joystick button N" to "joystick 4 button 19" (same 20-byte
slot: 17 chars + 3 pad -> 20 chars) and moves every joystick-axis entry to joystick 4, which never
exists on a handheld, so only gptokeyb2's keyboard/mouse input reaches the game. The file size
does not change, so the result can also ship as offset/bytes pairs patched in on the device.

usage: patch_input_axes.py <mainData>
  writes <mainData>.joy and prints the sha1 before/after and the "offset hex" pairs
  (Ittle Dew's patchscript applies them with its patch_bytes helper after a sha1 check).
Assumes the InputManager's first axis is the default "Horizontal". From Ittle Dew (2026-09-30).
"""
import struct, sys, hashlib

path = sys.argv[1]
data = bytearray(open(path, 'rb').read())
print('in  sha1', hashlib.sha1(data).hexdigest())

start = data.find(b'Horizontal') - 8          # int count, then first entry's name length
count = struct.unpack_from('<i', data, start)[0]
assert 0 < count < 200, count     # number of axes (19 in a default project)
pos = start + 4
pairs = []
NEW = b'joystick 4 button 19'
assert len(NEW) == 20

def rd_str(p):
    n = struct.unpack_from('<i', data, p)[0]
    s = bytes(data[p + 4:p + 4 + n])
    return s, p + 4 + ((n + 3) & ~3)

for i in range(count):
    fields = []
    for k in range(7):
        s, nxt = rd_str(pos)
        fields.append((pos, s))
        pos = nxt
    gravity, dead, sens = struct.unpack_from('<fff', data, pos)
    snap, inv = data[pos + 12], data[pos + 13]
    typ, axis, joy = struct.unpack_from('<iii', data, pos + 16)
    joy_off = pos + 24
    pos += 28
    name = fields[0][1].decode()
    print(f'{i:2d} {name:16s} type={typ} axis={axis} joy={joy} '
          + ' '.join(repr(s.decode()) for _, s in fields[3:7] if s))
    for off, s in fields[3:7]:
        if s.startswith(b'joystick button '):
            assert len(s) == 17, s
            pairs.append((off, struct.pack('<i', 20) + NEW))
    if typ == 2:
        pairs.append((joy_off, struct.pack('<i', 4)))

for off, blob in pairs:
    data[off:off + len(blob)] = blob
open(path + '.joy', 'wb').write(data)
print('out sha1', hashlib.sha1(data).hexdigest(), 'size', len(data))
print('pairs:', len(pairs))
print(' '.join(f'{off} {blob.hex()}' for off, blob in pairs))

# re-parse check
d = data; p = start + 4
for i in range(count):
    for k in range(7):
        n = struct.unpack_from('<i', d, p)[0]; s = bytes(d[p + 4:p + 4 + n]); p += 4 + ((n + 3) & ~3)
        assert not s.startswith(b'joystick button'), s
    typ, axis, joy = struct.unpack_from('<iii', d, p + 16); p += 28
    assert typ != 2 or joy == 4
print("re-parse OK; bytes after the axes:", bytes(d[p:p + 12]))
