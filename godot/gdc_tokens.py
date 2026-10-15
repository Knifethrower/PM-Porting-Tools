"""Decode Godot 4.3 binary GDScript (.gdc, tokenizer buffer v100) from a pck.

Prints, for each identifier of interest, the identifiers that follow it after one
token (e.g. `PlayFabServices . login`), to learn which members stub classes need.

usage: gdc_tokens.py <pck> <names,comma,separated>
"""
import struct, sys, collections, zstandard

TOKEN_BYTE_MASK = 0x80
TOKEN_BITS = 8
TOKEN_MASK = (1 << (TOKEN_BITS - 1)) - 1


def read_pck(path):
    f = open(path, 'rb')
    h = f.read(4 + 16 + 12 + 64)
    assert h[:4] == b'GDPC'
    fmt = struct.unpack_from('<I', h, 4)[0]
    assert fmt == 2, fmt
    flags, base = struct.unpack_from('<IQ', h, 20)
    f.seek(4 + 16 + 12 + 64)
    count = struct.unpack('<I', f.read(4))[0]
    files = {}
    for _ in range(count):
        n = struct.unpack('<I', f.read(4))[0]
        name = f.read(n).rstrip(b'\0').decode()
        off, size = struct.unpack('<QQ', f.read(16))
        f.read(16 + 4)
        files[name] = (off + base, size)
    return f, files


def skip_variant(d, p):
    t = struct.unpack_from('<I', d, p)[0]; p += 4
    typ, flag64 = t & 0xff, t & (1 << 16)
    if typ == 0:
        return p
    if typ == 1:
        return p + 4
    if typ in (2, 3):
        return p + (8 if flag64 else 4)
    if typ in (4, 21):  # String, StringName
        l = struct.unpack_from('<I', d, p)[0]; p += 4
        return p + l + ((4 - l % 4) % 4)
    real = 8 if flag64 else 4
    sizes = {5: 2 * real, 6: 8, 7: 4 * real, 8: 16, 9: 3 * real, 10: 12, 11: 4 * real, 12: 16,
             13: 6 * real, 14: 4 * real, 15: 6 * real, 16: 4 * real, 17: 12 * real, 18: 12 * real,
             19: 16 * real, 20: 16}
    if typ in sizes:
        return p + sizes[typ]
    raise ValueError(f'variant type {typ}')


def decode(b):
    assert b[:4] == b'GDSC'
    size = struct.unpack_from('<I', b, 8)[0]
    d = zstandard.ZstdDecompressor().decompress(b[12:], max_output_size=size) if size else b[12:]
    ic, cc, lines, cols, tc = struct.unpack_from('<5I', d, 0)
    p = 20
    ids = []
    for _ in range(ic):
        l = struct.unpack_from('<I', d, p)[0]; p += 4
        cs = struct.unpack_from('<%dI' % l, d, p); p += 4 * l
        ids.append(''.join(chr((c ^ 0xb6b6b6b6) & 0x1fffff) for c in cs))
    for _ in range(cc):
        p = skip_variant(d, p)
    p += 16 * lines  # line map + column map, both token_line_count entries
    toks = []
    for _ in range(tc):
        # 4.3: a token is 5 bytes, or 8 when the byte mask bit is set (data index in the upper bits)
        t = struct.unpack_from('<I', d, p)[0]
        typ = d[p] & TOKEN_MASK
        if d[p] & TOKEN_BYTE_MASK:
            toks.append((typ, (t & ~TOKEN_BYTE_MASK) >> TOKEN_BITS)); p += 8
        else:
            toks.append((typ, 0)); p += 5
    return ids, toks, p == len(d)


IDENT = 2

if __name__ == '__main__':
    f, files = read_pck(sys.argv[1])
    wanted = sys.argv[2].split(',')
    uses = collections.defaultdict(collections.Counter)
    where = collections.defaultdict(set)
    bad = 0
    for name, (off, size) in files.items():
        if not name.endswith('.gdc'):
            continue
        f.seek(off)
        try:
            ids, toks, ok = decode(f.read(size))
        except Exception as e:
            print('decode failed', name, e); bad += 1; continue
        if not ok:
            bad += 1
        for i, (t, x) in enumerate(toks):
            if t == IDENT and ids[x] in wanted:
                where[ids[x]].add(name.replace('res://', ''))
                nxt = toks[i + 1] if i + 1 < len(toks) else (None, None)
                nn = toks[i + 2] if i + 2 < len(toks) else (None, None)
                member = ids[nn[1]] if nn[0] == IDENT else f'<tok{nn[0]}>'
                uses[ids[x]][(nxt[0], member)] += 1
    print('scripts not fully consumed:', bad)
    for w in wanted:
        print(f'== {w} in {len(where[w])} scripts')
        for (sep, m), n in sorted(uses[w].items(), key=lambda kv: (kv[0][0] or 0, kv[0][1])):
            print(f'   sep_tok={sep} {m}  x{n}')
