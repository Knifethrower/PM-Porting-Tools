"""Prints the structure of a Vorbis stream's identification and setup headers (codebooks,
floors, residues, mappings, modes), to see which coding features a file uses.
usage: vorbis_setup.py file.ogg"""
import sys, struct

def ogg_packets(data, count):
    pos, cur, out = 0, b"", []
    while pos < len(data) and len(out) < count:
        assert data[pos:pos + 4] == b"OggS"
        nseg = data[pos + 26]
        lacing = data[pos + 27: pos + 27 + nseg]
        body = pos + 27 + nseg
        for l in lacing:
            cur += data[body: body + l]; body += l
            if l < 255:
                out.append(cur); cur = b""
        pos = body
    return out

class Bits:
    def __init__(self, b): self.b, self.p = b, 0
    def read(self, n):
        v = 0
        for i in range(n):
            byte = self.b[self.p >> 3]
            v |= ((byte >> (self.p & 7)) & 1) << i
            self.p += 1
        return v

def ilog(x):
    n = 0
    while x > 0: n += 1; x >>= 1
    return n

def float32(x):
    mant, exp, sign = x & 0x1fffff, (x & 0x7fe00000) >> 21, x & 0x80000000
    if sign: mant = -mant
    return mant * 2.0 ** (exp - 788)

def lookup1_values(entries, dim):
    r = 0
    while (r + 1) ** dim <= entries: r += 1
    return r

data = open(sys.argv[1], "rb").read()
ident, comment, setup = ogg_packets(data, 3)
ver, ch, rate, bmax, bnom, bmin, bs = struct.unpack("<IBIiiiB", ident[7:7 + 22])
print(f"vorbis version {ver}, {ch} ch, {rate} Hz, bitrate max/nominal/min {bmax}/{bnom}/{bmin}, "
      f"blocksizes {1 << (bs & 15)}/{1 << (bs >> 4)}")
vlen = struct.unpack("<I", comment[7:11])[0]
print("vendor:", comment[11:11 + vlen].decode("latin1"))

b = Bits(setup[7:])
nbooks = b.read(8) + 1
print(f"\n{nbooks} codebooks:")
for i in range(nbooks):
    sync = b.read(24); assert sync == 0x564342, hex(sync)
    dims, entries, ordered = b.read(16), b.read(24), b.read(1)
    sparse, lengths = 0, []
    if not ordered:
        sparse = b.read(1)
        for e in range(entries):
            if sparse:
                lengths.append(b.read(5) + 1 if b.read(1) else 0)
            else:
                lengths.append(b.read(5) + 1)
    else:
        cl, ce = b.read(5) + 1, 0
        while ce < entries:
            n = b.read(ilog(entries - ce)); lengths += [cl] * n; ce += n; cl += 1
    used = sum(1 for l in lengths if l)
    lt = b.read(4)
    desc = ""
    if lt in (1, 2):
        mn, dl, vb, seq = float32(b.read(32)), float32(b.read(32)), b.read(4) + 1, b.read(1)
        nv = lookup1_values(entries, dims) if lt == 1 else entries * dims
        vals = [b.read(vb) for _ in range(nv)]
        desc = f" lookup {lt} min {mn:g} delta {dl:g} value_bits {vb} sequence_p {seq} values {nv}"
    elif lt != 0:
        desc = f" LOOKUP TYPE {lt}?"
    single = " SINGLE-ENTRY" if used == 1 else ""
    print(f"  book {i:2d}: dims {dims} entries {entries} used {used} max_len {max(lengths)} "
          f"{'ordered' if ordered else ('sparse' if sparse else 'dense')}{single}{desc}")

ntimes = b.read(6) + 1
assert all(b.read(16) == 0 for _ in range(ntimes))
nfloors = b.read(6) + 1
print(f"\n{nfloors} floors:")
for i in range(nfloors):
    ft = b.read(16)
    if ft == 0:
        order, frate, bark, ampbits, ampoff, nb = b.read(8), b.read(16), b.read(16), b.read(6), b.read(8), b.read(4) + 1
        books = [b.read(8) for _ in range(nb)]
        print(f"  floor {i}: TYPE 0 order {order} rate {frate} bark_map {bark} amp_bits {ampbits} books {books}")
    else:
        parts = b.read(5); pcl = [b.read(4) for _ in range(parts)]
        maxc = max(pcl) if pcl else -1
        cdims = []
        for c in range(maxc + 1):
            d, sub = b.read(3) + 1, b.read(2); cdims.append(d)
            if sub: b.read(8)
            for _ in range(1 << sub): b.read(8)
        mult, rb = b.read(2) + 1, b.read(4)
        xs = sum(cdims[c] for c in pcl)
        for _ in range(xs): b.read(rb)
        print(f"  floor {i}: type 1, {parts} partitions, multiplier {mult}, {xs + 2} points")
nres = b.read(6) + 1
print(f"\n{nres} residues:")
for i in range(nres):
    rt = b.read(16)
    beg, end, psize, ncls, cbook = b.read(24), b.read(24), b.read(24) + 1, b.read(6) + 1, b.read(8)
    casc = []
    for _ in range(ncls):
        lo, flag = b.read(3), b.read(1)
        casc.append(lo | ((b.read(5) << 3) if flag else 0))
    books = [[b.read(8) if (c >> k) & 1 else None for k in range(8)] for c in casc]
    print(f"  residue {i}: type {rt} range {beg}-{end} partition {psize} classifications {ncls} classbook {cbook}")
nmap = b.read(6) + 1
print(f"\n{nmap} mappings:")
for i in range(nmap):
    mt = b.read(16)
    sub = b.read(4) + 1 if b.read(1) else 1
    steps = b.read(8) + 1 if b.read(1) else 0
    for _ in range(steps): b.read(ilog(ch - 1)); b.read(ilog(ch - 1))
    b.read(2)
    if sub > 1: [b.read(4) for _ in range(ch)]
    sm = [(b.read(8), b.read(8), b.read(8)) for _ in range(sub)]
    print(f"  mapping {i}: type {mt}, submaps {sub}, coupling steps {steps}, (_, floor, residue) {sm}")
nmodes = b.read(6) + 1
print(f"\n{nmodes} modes:")
for i in range(nmodes):
    bf, wt, tt, mp = b.read(1), b.read(16), b.read(16), b.read(8)
    print(f"  mode {i}: {'long' if bf else 'short'} block, mapping {mp}")
print("framing bit:", b.read(1))
