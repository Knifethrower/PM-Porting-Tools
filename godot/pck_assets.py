#!/usr/bin/env python3
"""List every imported asset of a Godot 4 pack: source path, imported type (the remap target's
extension: ctex, sample, oggvorbisstr, fontdata, res, scn, ...) and the imported file's size. Sorted by size,
biggest first, so the RAM levers show up at the top (Dome Keeper: 708 MB of uncompressed samples).
The output is also the asset list probe/MemProbe.gd reads (probe/assets.tsv).

usage: pck_assets.py <pck> [> assets.tsv]
From Dome Keeper. License: 0BSD.
"""
import re, sys
import pck_patch as P

if len(sys.argv) != 2:
    sys.exit(__doc__)
rows = []
with open(sys.argv[1], 'rb') as f:
    base, entries = P.read_dir(f)
    for path in entries:
        if not path.endswith(('.import', '.remap')):     # imported assets; exported .tres/.tscn
            continue
        _, off, size = entries[path]
        f.seek(base + off)
        m = re.search(r'^path(?:\.\w+)?="([^"]+)"', f.read(size).decode('utf-8', 'replace'), re.M)
        if not m or m.group(1) not in entries:
            continue
        real = m.group(1)
        rows.append((path.rsplit('.', 1)[0], real.rsplit('.', 1)[-1], entries[real][2]))
for src, typ, size in sorted(rows, key=lambda r: -r[2]):
    print(f'{src}\t{typ}\t{size}')
