"""Read / patch Gone Home's Options.sav (.NET BinaryFormatter, class GameOptions).

Only the record shapes this file uses are handled: one ClassWithMembersAndTypes (GameOptions)
whose members are primitives, two nested classes (UResolution {width, height}, the
ControlScheme enum) and a string (Language). Values are patched in place, so the layout
never changes.

Usage: python options_sav.py <Options.sav> [Name=value ...] [--out file]
       (Res.width / Res.height address the resolution)
"""
import struct, sys

PRIM = {1: ('<?', 1), 2: ('<B', 1), 6: ('<d', 8), 7: ('<h', 2), 8: ('<i', 4), 9: ('<q', 8),
        11: ('<f', 4), 14: ('<H', 2), 15: ('<I', 4), 16: ('<Q', 8)}


class Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def i32(self):
        v, = struct.unpack_from('<i', self.d, self.p); self.p += 4; return v

    def string(self):
        n, shift = 0, 0
        while True:
            b = self.u8(); n |= (b & 0x7f) << shift; shift += 7
            if not b & 0x80: break
        s = self.d[self.p:self.p + n].decode('utf-8'); self.p += n; return s


def parse(data):
    """Returns {name: (offset, fmt, value)} for every primitive field (nested as 'Res.width')."""
    r = Reader(data)
    fields, classes = {}, {}

    def class_record(prefix):
        rt = r.u8()
        if rt == 5:                                   # ClassWithMembersAndTypes
            obj_id = r.i32(); name = r.string()
            names = [r.string() for _ in range(r.i32())]
            btypes = [r.u8() for _ in names]
            extra = []
            for bt in btypes:
                if bt == 0: extra.append(r.u8())                 # primitive type
                elif bt == 3: extra.append(r.string())            # system class name
                elif bt == 4: extra.append((r.string(), r.i32())) # class name + library id
                elif bt == 7: extra.append(r.u8())
                else: extra.append(None)
            r.i32()                                    # library id
            classes[obj_id] = (names, btypes, extra)
        elif rt == 1:                                  # ClassWithId (repeat of a known layout)
            obj_id = r.i32(); ref = r.i32()
            names, btypes, extra = classes[ref]
        else:
            raise ValueError(f'unsupported record {rt} at {r.p - 1}')
        for n, bt, ex in zip(names, btypes, extra):
            key = prefix + n
            if bt == 0:
                fmt, size = PRIM[ex]
                v, = struct.unpack_from(fmt, data, r.p)
                fields[key] = (r.p, fmt, v); r.p += size
            elif bt in (3, 4):
                class_record(key + '.')
            elif bt == 1:                              # string
                rt2 = r.u8()
                if rt2 == 6:
                    r.i32(); fields[key] = (None, 'str', r.string())
                elif rt2 == 10:
                    fields[key] = (None, 'str', None)
                else:
                    raise ValueError(f'unsupported string record {rt2}')
            else:
                raise ValueError(f'unsupported member type {bt} for {key}')

    assert r.u8() == 0; r.p += 16                     # SerializationHeaderRecord
    assert r.u8() == 12; r.i32(); r.string()          # BinaryLibrary
    class_record('')
    return fields


def patch(data, changes):
    data = bytearray(data)
    fields = parse(bytes(data))
    for k, v in changes.items():
        off, fmt, old = fields[k]
        if off is None:
            raise ValueError(f'{k} cannot be patched')
        struct.pack_into(fmt, data, off, type(old)(v) if not isinstance(old, bool) else bool(int(v)))
    return bytes(data)


if __name__ == '__main__':
    args = sys.argv[1:]
    out = None
    if '--out' in args:
        i = args.index('--out'); out = args[i + 1]; del args[i:i + 2]
    path, sets = args[0], dict(a.split('=', 1) for a in args[1:])
    data = open(path, 'rb').read()
    if sets:
        data = patch(data, {k: float(v) if '.' in v else int(v) for k, v in sets.items()})
        open(out or path, 'wb').write(data)
    for k, (off, fmt, v) in parse(data).items():
        print(f'{k} = {v}')
