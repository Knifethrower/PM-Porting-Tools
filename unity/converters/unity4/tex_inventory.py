"""Unity 4 (UnityPy): texture inventory every Texture2D >= 512 px with name, size, format and DXT bytes, grouped by
size, so a shrink policy can be chosen by what the textures are. Usage: tex_inventory.py <Game_Data dir>  (from Ittle Dew)"""
import glob, os, struct, sys, collections
import UnityPy

DATA = sys.argv[1]
files = [os.path.join(DATA, 'resources.assets')] + sorted(glob.glob(os.path.join(DATA, 'sharedassets*.assets')))
rows = []
for f in files:
    env = UnityPy.load(f)
    for o in env.objects:
        if o.type.name != 'Texture2D':
            continue
        raw = o.get_raw_data()
        n = struct.unpack_from('<i', raw, 0)[0]
        name = raw[4:4 + n].decode('utf-8', 'replace')
        p = 4 + ((n + 3) & ~3)
        w, h, size, fmt = struct.unpack_from('<iiii', raw, p)
        if max(w, h) >= 512:
            rows.append((os.path.basename(f), name, w, h, fmt, size))
FMT = {10: 'DXT1', 12: 'DXT5', 1: 'A8', 3: 'RGB24', 4: 'RGBA32', 5: 'ARGB32', 7: 'RGB565', 13: 'RGBA4444'}
by = collections.defaultdict(list)
for r in rows:
    by[max(r[2], r[3])].append(r)
for s in sorted(by, reverse=True):
    tot = sum(r[5] for r in by[s]) / 1048576
    print(f'\n== larger side {s}: {len(by[s])} textures, {tot:.1f} MB')
    for f, name, w, h, fmt, size in sorted(by[s], key=lambda r: -r[5]):
        print(f'   {size / 1048576:6.1f} MB {w}x{h} {FMT.get(fmt, fmt):5} {name[:40]:40} {f}')
