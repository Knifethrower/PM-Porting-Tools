"""Minimal reader/rewriter for Unity 4 serialized files (version 9, little-endian data), low memory.

Layout: big-endian header {metadata size, file size, version, data offset, endianness}, metadata
holding the object table 'int count; count x {int pathID, uint byteStart, uint byteSize,
int typeID, short classID, short destroyed}' (byteStart relative to the data offset), then the
objects in table order, each aligned to 8 bytes.

    with Unity4File(path) as uf:
        for i, e in enumerate(uf.entries): raw = uf.raw(i)
        uf.rewrite({index: new_object_bytes})     # streams <path>.tmp, then replaces <path>
"""
import mmap, os, struct


class Unity4File:
    def __init__(self, path):
        self.path = path
        self.f = open(path, 'rb')
        self.mm = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        _, file_size, version, self.data_off = struct.unpack_from('>IIII', self.mm, 0)
        if version != 9 or self.mm[16] != 0 or file_size != len(self.mm):
            raise ValueError(f'{path}: not a little-endian Unity 4 (version 9) serialized file')
        self.table, count = self._find_table()
        self.entries = [struct.unpack_from('<iIIihh', self.mm, self.table + 4 + 20 * i) for i in range(count)]

    def _find_table(self):
        mm, meta_end, data_len = self.mm, self.data_off, len(self.mm) - self.data_off
        for p in range(20, meta_end - 24, 4):
            n, = struct.unpack_from('<i', mm, p)
            if n <= 0 or p + 4 + 20 * n > meta_end:
                continue
            pos = 0
            for i in range(n):
                _, start, size = struct.unpack_from('<iII', mm, p + 4 + 20 * i)
                if start != ((pos + 7) & ~7 if i else 0):
                    break
                pos = start + size
            else:
                if pos == data_len:
                    return p, n
        raise ValueError(f'{self.path}: object table not found')

    def raw(self, i):
        _, start, size, _, _, _ = self.entries[i]
        return self.mm[self.data_off + start:self.data_off + start + size]

    def find(self, class_id):
        return [i for i, e in enumerate(self.entries) if e[4] == class_id]

    def rewrite(self, new):
        """Write the file with objects {index: bytes} replaced, then replace the original."""
        head = bytearray(self.mm[:self.data_off])
        layout, pos = [], 0
        for i, e in enumerate(self.entries):
            pos = (pos + 7) & ~7
            size = len(new[i]) if i in new else e[2]
            struct.pack_into('<iIIihh', head, self.table + 4 + 20 * i, e[0], pos, size, e[3], e[4], e[5])
            layout.append((pos, size))
            pos += size
        struct.pack_into('>I', head, 4, self.data_off + pos)
        with open(self.path + '.tmp', 'wb') as out:
            out.write(head)
            written = 0
            for i, (start, size) in enumerate(layout):
                out.write(b'\0' * (start - written))
                if i in new:
                    out.write(new[i])
                else:
                    src, left = self.data_off + self.entries[i][1], size
                    while left:
                        n = min(left, 1 << 22)
                        out.write(self.mm[src:src + n]); src += n; left -= n
                written = start + size
            out.flush(); os.fsync(out.fileno())
        self.close()
        os.replace(self.path + '.tmp', self.path)

    def close(self):
        if self.mm:
            self.mm.close(); self.f.close(); self.mm = None

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()
