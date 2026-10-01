#!/usr/bin/env python3
"""Lossless Vorbis residue repacker: rewrites residue type 0 as residue type 1.

Residue type 0 (used by pre-1.0 libvorbis encoders) spreads the values of one VQ codeword
over a partition (value j of codeword i goes to position i + j*step); type 1 puts them next
to each other. Both carry the same integer lattice values, so a stream can be converted
without decoding to PCM: every audio packet is parsed, the floor and all other bits are
copied unchanged, and the residue values of each partition and pass are regrouped into
consecutive tuples and written with the codeword of the same book that has exactly those
values. The two residue type fields in the setup header change from 0 to 1. Decoded PCM is
identical (check with --verify or by decoding both files).

usage: repack.py in.ogg out.ogg
"""
import sys, struct

# ---------------------------------------------------------------- bits

class Reader:
    """LSB-first bit reader over a packet."""
    def __init__(self, data):
        self.v = int.from_bytes(data, "little")
        self.n = len(data) * 8
        self.p = 0
    def read(self, k):
        if self.p + k > self.n:
            raise EOFError
        x = (self.v >> self.p) & ((1 << k) - 1)
        self.p += k
        return x

class Writer:
    def __init__(self):
        self.v = 0
        self.p = 0
    def write(self, x, k):
        self.v |= (x & ((1 << k) - 1)) << self.p
        self.p += k
    def copy(self, reader, start, end):
        """Copies bits [start, end) of a reader."""
        k = end - start
        if k:
            self.write((reader.v >> start) & ((1 << k) - 1), k)
    def bytes(self):
        return self.v.to_bytes((self.p + 7) // 8, "little")

def ilog(x):
    n = 0
    while x > 0:
        n += 1
        x >>= 1
    return n

# ---------------------------------------------------------------- codebooks

class Codebook:
    def __init__(self, dims, lengths, lookup, mults, minimum=0.0, delta=0.0):
        self.dims, self.lengths, self.lookup, self.mults = dims, lengths, lookup, mults
        self.minimum, self.delta = minimum, delta
        self.entries = len(lengths)
        self.codes = make_codewords(lengths)
        # decode table keyed by (length, codeword bits in stream order)
        self.table = {}
        self.rev = [0] * self.entries
        for e, (l, c) in enumerate(zip(lengths, self.codes)):
            if l:
                r = int(format(c, "0%db" % l)[::-1], 2)  # stream order = MSB of codeword first
                self.rev[e] = r
                self.table[(l, r)] = e
        self.lens_sorted = sorted(set(l for l in lengths if l))
        self.maxlen = max(self.lens_sorted)
        if lookup:
            # the vector of values of every used entry (sequence_p is 0 for all books here,
            # so a value is minimum + delta * multiplicand); the shortest codeword per vector
            self.by_vals = {}
            for e in range(self.entries):
                if not lengths[e]:
                    continue
                t = self.vals_of(e)
                best = self.by_vals.get(t)
                if best is None or lengths[e] < lengths[best]:
                    self.by_vals[t] = e
            self.value_set = sorted(set(self.value(m) for m in mults))
            self.maxabs = max(abs(v) for v in self.value_set)
    def value(self, m):
        v = self.minimum + self.delta * m
        assert v == int(v), "non-integer VQ value"
        return int(v)
    def vals_of(self, e):
        return tuple(self.value(m) for m in self.tuple_of(e))
    def tuple_of(self, e):
        if self.lookup == 1:
            L = len(self.mults)
            out, div = [], 1
            for j in range(self.dims):
                out.append(self.mults[(e // div) % L])
                div *= L
            return tuple(out)
        return tuple(self.mults[e * self.dims + j] for j in range(self.dims))
    def decode(self, r):
        avail = r.n - r.p
        peek = (r.v >> r.p) & ((1 << min(self.maxlen, avail)) - 1) if avail > 0 else 0
        for l in self.lens_sorted:
            if l > avail:
                break
            e = self.table.get((l, peek & ((1 << l) - 1)))
            if e is not None:
                r.p += l
                return e
        raise EOFError
    def encode(self, w, e):
        w.write(self.rev[e], self.lengths[e])

def make_codewords(lengths):
    """Vorbis codeword assignment (as libvorbis _make_words)."""
    marker = [0] * 33
    codes = [0] * len(lengths)
    for i, l in enumerate(lengths):
        if l <= 0:
            continue
        entry = marker[l]
        if l < 32 and (entry >> l):
            raise ValueError("overspecified codebook")
        codes[i] = entry
        for j in range(l, 0, -1):
            if marker[j] & 1:
                marker[j] = marker[1] + 1 if j == 1 else marker[j - 1] << 1
                break
            marker[j] += 1
        for j in range(l + 1, 33):
            if (marker[j] >> 1) == entry:
                entry = marker[j]
                marker[j] = marker[j - 1] << 1
            else:
                break
    return codes

def float32(x):
    """Vorbis packed float: 21-bit mantissa, 10-bit exponent, sign."""
    mant, exp = x & 0x1fffff, (x & 0x7fe00000) >> 21
    if x & 0x80000000:
        mant = -mant
    return mant * 2.0 ** (exp - 788)

def lookup1_values(entries, dims):
    r = 0
    while (r + 1) ** dims <= entries:
        r += 1
    return r

# ---------------------------------------------------------------- setup header

class Setup:
    pass

def parse_setup(packet, channels):
    s = Setup()
    r = Reader(packet)
    assert r.read(8) == 5 and packet[1:7] == b"vorbis"
    r.read(48)
    s.books = []
    for _ in range(r.read(8) + 1):
        assert r.read(24) == 0x564342
        dims, entries, ordered = r.read(16), r.read(24), r.read(1)
        lengths = []
        if not ordered:
            sparse = r.read(1)
            for _ in range(entries):
                if sparse:
                    lengths.append(r.read(5) + 1 if r.read(1) else 0)
                else:
                    lengths.append(r.read(5) + 1)
        else:
            cl, ce = r.read(5) + 1, 0
            while ce < entries:
                k = r.read(ilog(entries - ce))
                lengths += [cl] * k
                ce += k
                cl += 1
        lookup, mults, mn, dl = r.read(4), None, 0.0, 0.0
        if lookup in (1, 2):
            mn, dl = float32(r.read(32)), float32(r.read(32))
            vb, seq = r.read(4) + 1, r.read(1)
            assert seq == 0, "sequence_p books not handled"
            nv = lookup1_values(entries, dims) if lookup == 1 else entries * dims
            mults = [r.read(vb) for _ in range(nv)]
        else:
            assert lookup == 0
        s.books.append(Codebook(dims, lengths, lookup, mults, mn, dl))
    for _ in range(r.read(6) + 1):
        assert r.read(16) == 0
    s.floors = []
    for _ in range(r.read(6) + 1):
        ft = r.read(16)
        assert ft == 1, "floor 0 not handled"
        f = Setup()
        f.pcl = [r.read(4) for _ in range(r.read(5))]
        f.cdim, f.csub, f.cmaster, f.subbooks = [], [], [], []
        for _ in range((max(f.pcl) + 1) if f.pcl else 0):
            f.cdim.append(r.read(3) + 1)
            sub = r.read(2)
            f.csub.append(sub)
            f.cmaster.append(r.read(8) if sub else None)
            f.subbooks.append([r.read(8) - 1 for _ in range(1 << sub)])
        f.mult = r.read(2) + 1
        rb = r.read(4)
        for c in f.pcl:
            for _ in range(f.cdim[c]):
                r.read(rb)
        f.rangebits = ilog([256, 128, 86, 64][f.mult - 1] - 1)
        s.floors.append(f)
    s.residues, s.residue_type_pos = [], []
    for _ in range(r.read(6) + 1):
        s.residue_type_pos.append(r.p)
        res = Setup()
        res.type = r.read(16)
        res.begin, res.end, res.psize = r.read(24), r.read(24), r.read(24) + 1
        res.ncls, res.classbook = r.read(6) + 1, r.read(8)
        casc = []
        for _ in range(res.ncls):
            lo, flag = r.read(3), r.read(1)
            casc.append(lo | ((r.read(5) << 3) if flag else 0))
        res.books = [[r.read(8) if (c >> k) & 1 else None for k in range(8)] for c in casc]
        s.residues.append(res)
    s.mappings = []
    for _ in range(r.read(6) + 1):
        assert r.read(16) == 0
        sub = r.read(4) + 1 if r.read(1) else 1
        steps = r.read(8) + 1 if r.read(1) else 0
        assert steps == 0, "channel coupling not handled"
        assert r.read(2) == 0
        mux = [r.read(4) for _ in range(channels)] if sub > 1 else [0] * channels
        subs = []
        for _ in range(sub):
            r.read(8)
            subs.append((r.read(8), r.read(8)))
        s.mappings.append((mux, subs))
    s.modes = []
    for _ in range(r.read(6) + 1):
        bf = r.read(1)
        r.read(16); r.read(16)
        s.modes.append((bf, r.read(8)))
    assert r.read(1) == 1
    return s

def setup_as_type1(packet, s):
    w = bytearray(packet)
    v = int.from_bytes(w, "little")
    for pos in s.residue_type_pos:
        assert (v >> pos) & 0xffff == 0
        v |= 1 << pos  # 16-bit field 0 -> 1
    return v.to_bytes(len(w), "little")

# ---------------------------------------------------------------- audio packets

class Stats:
    packets = repacked = codewords = reclassified = 0

def floor1_skip(r, f, books):
    """Reads past a floor 1; returns whether the floor is used."""
    if not r.read(1):
        return False
    r.read(f.rangebits); r.read(f.rangebits)
    for c in f.pcl:
        cbits = f.csub[c]
        cval = books[f.cmaster[c]].decode(r) if cbits else 0
        for _ in range(f.cdim[c]):
            b = f.subbooks[c][cval & ((1 << cbits) - 1)]
            cval >>= cbits
            if b >= 0:
                books[b].decode(r)
    return True

def plan_class(s, res, cls, contrib, total):
    """Codewords per pass for one partition in type 1 layout and class cls: from the
    per-pass values (contrib), or by decomposing the summed values (total) over the passes.
    None if some tuple has no codeword."""
    passes = [(p, s.books[b]) for p, b in enumerate(res.books[cls]) if b is not None]
    if total is not None:
        if not passes:
            return [None] * 8 if not any(total) else None
        caps = [sum(bk.maxabs for _, bk in passes[k + 1:]) for k in range(len(passes))]
        per = {p: [0] * res.psize for p, _ in passes}
        for x in range(res.psize):
            rem = total[x]
            for k, (p, bk) in enumerate(passes):
                ok = [v for v in bk.value_set if abs(rem - v) <= caps[k]]
                if not ok:
                    return None
                v = min(ok, key=lambda v: abs(rem - v))
                per[p][x] = v
                rem -= v
            if rem:
                return None
        contrib = [per.get(p) for p in range(8)]
    plan = [None] * 8
    for p in range(8):
        b = res.books[cls][p]
        if b is None:
            if contrib[p] is not None:
                return None
            continue
        bk = s.books[b]
        vals = contrib[p] if contrib[p] is not None else [0] * res.psize
        es = []
        for k in range(res.psize // bk.dims):
            e = bk.by_vals.get(tuple(vals[k * bk.dims: (k + 1) * bk.dims]))
            if e is None:
                return None
            es.append(e)
        plan[p] = es
    return plan

def plan_bits(s, res, cls, plan):
    return sum(s.books[res.books[cls][p]].lengths[e] for p in range(8) if plan[p] for e in plan[p])

def repack_packet(packet, s, channels, blocksizes, stats):
    r = Reader(packet)
    w = Writer()
    if r.read(1) != 0:
        raise ValueError("not an audio packet")
    mode_bits = ilog(len(s.modes) - 1)
    bf, mapping = s.modes[r.read(mode_bits)]
    if bf:
        r.read(2)
    n = blocksizes[bf]
    mux, subs = s.mappings[mapping]
    used = []
    for ch in range(channels):
        used.append(floor1_skip(r, s.floors[subs[mux[ch]][0]], s.books))
    # everything up to here is copied unchanged
    w.copy(r, 0, r.p)
    for si, (_, resno) in enumerate(subs):
        chans = [ch for ch in range(channels) if mux[ch] == si]
        res = s.residues[resno]
        assert res.type == 0 and len(chans) == 1, "only type 0, one channel per submap"
        if not used[chans[0]]:
            continue
        actual = n // 2
        lb, le = min(res.begin, actual), min(res.end, actual)
        nparts = (le - lb) // res.psize
        if nparts == 0:
            continue
        cb = s.books[res.classbook]
        cw = cb.dims
        # read the whole residue: classes, and per partition and pass the values it adds
        classes = [0] * (nparts + cw)
        contrib = [[None] * 8 for _ in range(nparts)]
        for pss in range(8):
            pc = 0
            while pc < nparts:
                if pss == 0:
                    t = cb.decode(r)
                    for i in range(cw - 1, -1, -1):
                        classes[i + pc] = t % res.ncls
                        t //= res.ncls
                i = 0
                while i < cw and pc < nparts:
                    b = res.books[classes[pc]][pss]
                    if b is not None:
                        book = s.books[b]
                        d = book.dims
                        step = res.psize // d
                        # type 0: codeword k carries positions k, k+step, k+2*step, ...
                        vals = [0] * res.psize
                        for k in range(step):
                            t = book.vals_of(book.decode(r))
                            for j in range(d):
                                vals[k + j * step] = t[j]
                        contrib[pc][pss] = vals
                    i += 1
                    pc += 1
        # per partition: the same class with the values regrouped (type 1: codeword k
        # carries positions k*d .. k*d+d-1), or, where a regrouped tuple has no codeword,
        # the partition's summed values decomposed over the passes of another class
        plans = []
        for pc in range(nparts):
            plan = plan_class(s, res, classes[pc], contrib[pc], None)
            if plan is None:
                total = [0] * res.psize
                for v in contrib[pc]:
                    if v is not None:
                        total = [a + b for a, b in zip(total, v)]
                best = None
                for c in range(res.ncls):
                    if c != classes[pc]:
                        q = plan_class(s, res, c, None, total)
                        if q is not None and (best is None or plan_bits(s, res, c, q) < best[0]):
                            best = (plan_bits(s, res, c, q), c, q)
                if best is None:
                    raise LookupError("partition %d of packet %d cannot be repacked" % (pc, stats.packets))
                classes[pc] = best[1]
                plan = best[2]
                stats.reclassified += 1
            plans.append(plan)
        # write in stream order
        for pss in range(8):
            pc = 0
            while pc < nparts:
                if pss == 0:
                    t = 0
                    for i in range(cw):
                        t = t * res.ncls + classes[pc + i]
                    assert cb.lengths[t], "classification without a codeword"
                    cb.encode(w, t)
                i = 0
                while i < cw and pc < nparts:
                    b = res.books[classes[pc]][pss]
                    if b is not None:
                        for e in plans[pc][pss]:
                            s.books[b].encode(w, e)
                            stats.codewords += 1
                    i += 1
                    pc += 1
    stats.repacked += 1
    if r.n - r.p >= 8:
        raise ValueError("%d bits left after the residue" % (r.n - r.p))
    return w.bytes()

# ---------------------------------------------------------------- ogg

def crc32_ogg(data, table=[]):
    if not table:
        for i in range(256):
            c = i << 24
            for _ in range(8):
                c = ((c << 1) ^ 0x04C11DB7) & 0xffffffff if c & 0x80000000 else (c << 1) & 0xffffffff
            table.append(c)
    c = 0
    for b in data:
        c = ((c << 8) & 0xffffffff) ^ table[((c >> 24) ^ b) & 0xff]
    return c

def read_ogg(data):
    """Returns (serial, pages) with each page as (granule, [completed packets])."""
    pos, cur, pages, serial = 0, b"", [], None
    while pos < len(data):
        assert data[pos:pos + 4] == b"OggS", "bad page at %d" % pos
        _, _, flags, granule, ser, _, _, nseg = struct.unpack("<4sBBqIIIB", data[pos:pos + 27])
        assert not (flags & 1) or cur, "continued page without a packet in progress"
        serial = ser if serial is None else serial
        lacing = data[pos + 27: pos + 27 + nseg]
        body = pos + 27 + nseg
        done = []
        for l in lacing:
            cur += data[body: body + l]
            body += l
            if l < 255:
                done.append(cur)
                cur = b""
        pages.append((granule, done))
        pos = body
    assert cur == b"", "stream ends inside a packet"
    return serial, pages

def write_page(out, serial, seq, granule, packets, flags):
    lacing = bytearray()
    for p in packets:
        lacing += b"\xff" * (len(p) // 255) + bytes([len(p) % 255])
    assert len(lacing) <= 255
    head = struct.pack("<4sBBqIIIB", b"OggS", 0, flags, granule, serial, seq, 0, len(lacing))
    page = bytearray(head + bytes(lacing) + b"".join(packets))
    struct.pack_into("<I", page, 22, crc32_ogg(page))
    out += page

def paginate(packets):
    """Groups packets so that each group fits the 255 lacing values of one page."""
    group, segs, groups = [], 0, []
    for p in packets:
        k = len(p) // 255 + 1
        if segs + k > 255 and group:
            groups.append(group)
            group, segs = [], 0
        group.append(p)
        segs += k
    groups.append(group)
    return groups

def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    serial, pages = read_ogg(data)
    packets = [p for _, ps in pages for p in ps]
    ident, setup_pkt = packets[0], packets[2]
    channels = ident[11]
    bs = ident[28]
    blocksizes = (1 << (bs & 15), 1 << (bs >> 4))
    s = parse_setup(setup_pkt, channels)
    stats = Stats()
    new_setup = setup_as_type1(setup_pkt, s)
    out = bytearray()
    seq = 0
    pkt_index = 0
    last = len(pages) - 1
    for pi, (granule, ps) in enumerate(pages):
        if not ps:
            raise ValueError("page %d completes no packet; not handled" % pi)
        new = []
        for p in ps:
            if pkt_index == 2:
                new.append(new_setup)
            elif pkt_index < 2:
                new.append(p)
            else:
                stats.packets += 1
                new.append(repack_packet(p, s, channels, blocksizes, stats))
            pkt_index += 1
        groups = paginate(new)
        if len(groups) > 1:
            raise ValueError("page %d would need splitting" % pi)
        flags = (2 if pi == 0 else 0) | (4 if pi == last else 0)
        write_page(out, serial, seq, granule, new, flags)
        seq += 1
    open(dst, "wb").write(out)
    print("%s: %d audio packets repacked, %d codewords, %d partitions reclassified, %d -> %d bytes" %
          (src.split("/")[-1], stats.repacked, stats.codewords, stats.reclassified, len(data), len(out)))

if __name__ == "__main__":
    main()
