"""Print a Godot 4.3 .gdc from a pck as approximate GDScript source (for reading only).

usage: gdc_print.py <pck> <res://path.gdc>
"""
import struct, sys, zstandard
import gdc_tokens as G

NAMES = ['', '@', None, None, '<', '<=', '>', '>=', '==', '!=', 'and', 'or', 'not', '&&', '||', '!',
         '&', '|', '~', '^', '<<', '>>', '+', '-', '*', '**', '/', '%',
         '=', '+=', '-=', '*=', '**=', '/=', '%=', '<<=', '>>=', '&=', '|=', '^=',
         'if', 'elif', 'else', 'for', 'while', 'break', 'continue', 'pass', 'return', 'match', 'when',
         'as', 'assert', 'await', 'breakpoint', 'class', 'class_name', 'const', 'enum', 'extends', 'func',
         'in', 'is', 'namespace', 'preload', 'self', 'signal', 'static', 'super', 'trait', 'var', 'void', 'yield',
         '[', ']', '{', '}', '(', ')', ',', ';', '.', '..', ':', '$', '->', '_',
         '\n', '<INDENT>', '<DEDENT>', 'PI', 'TAU', 'INF', 'NAN', '<VCS>', '`', '?', '<ERR>', '<EOF>']


def read_constants(d, p, cc):
    out = []
    for _ in range(cc):
        t = struct.unpack_from('<I', d, p)[0]
        typ, f64 = t & 0xff, t & (1 << 16)
        q = p + 4
        if typ in (4, 21):
            l = struct.unpack_from('<I', d, q)[0]
            out.append(('&' if typ == 21 else '') + repr(d[q + 4:q + 4 + l].decode('utf-8', 'replace')))
        elif typ == 2:
            out.append(str(struct.unpack_from('<q' if f64 else '<i', d, q)[0]))
        elif typ == 3:
            out.append(str(struct.unpack_from('<d' if f64 else '<f', d, q)[0]))
        elif typ == 1:
            out.append('true' if struct.unpack_from('<I', d, q)[0] else 'false')
        elif typ == 0:
            out.append('null')
        else:
            out.append(f'<variant {typ}>')
        p = G.skip_variant(d, p)
    return out, p


def main(pck, path):
    f, files = G.read_pck(pck)
    off, size = files[path]
    f.seek(off)
    b = f.read(size)
    usize = struct.unpack_from('<I', b, 8)[0]
    d = zstandard.ZstdDecompressor().decompress(b[12:], max_output_size=usize) if usize else b[12:]
    ids, toks, _ = G.decode(b)
    ic, cc, lines = struct.unpack_from('<3I', d, 0)
    p = 20
    for _ in range(ic):
        l = struct.unpack_from('<I', d, p)[0]; p += 4 + 4 * l
    consts, _ = read_constants(d, p, cc)
    indent = 0
    line = []
    out = []
    for t, x in toks:
        if t == 2:
            line.append(ids[x])
        elif t == 3:
            line.append(consts[x])
        elif t == 87:
            out.append('\t' * indent + ' '.join(line)); line = []
        elif t == 88:
            indent += 1
        elif t == 89:
            indent -= 1
        elif t < len(NAMES):
            line.append(NAMES[t])
        else:
            line.append(f'#{t}')
    out.append('\t' * indent + ' '.join(line))
    print('\n'.join(l for l in out if l.strip()))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
