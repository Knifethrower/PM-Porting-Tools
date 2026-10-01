import UnityPy, glob, collections, json, sys
fmt = collections.Counter(); fmt_n = collections.Counter(); sizes = collections.Counter(); big = []
seen = set(); total = 0; n = 0
for f in sorted(glob.glob('sharedassets*.assets')) + ['resources.assets']:
    env = UnityPy.load(f)
    for o in env.objects:
        if o.type.name != 'Texture2D': continue
        t = o.read_typetree()
        key = (f, o.path_id)
        sz = t.get('m_CompleteImageSize', 0); w, h = t['m_Width'], t['m_Height']
        fm = t['m_TextureFormat']; n += 1; total += sz
        fmt[fm] += sz; fmt_n[fm] += 1
        m = max(w, h); sizes['>=4096' if m >= 4096 else '2048' if m >= 2048 else '1024' if m >= 1024 else '<1024'] += sz
        big.append((sz, w, h, fm, t['m_MipCount'], t['m_Name'], f))
names = {1:'Alpha8',3:'RGB24',4:'RGBA32',5:'ARGB32',7:'RGB565',10:'DXT1',12:'DXT5',13:'RGBA4444',14:'BGRA32',25:'BC7',34:'ETC_RGB4',47:'ETC2_RGBA8'}
print(f'{n} textures, {total/1e9:.2f} GB of pixel data')
for k, v in fmt.most_common(): print(f'  {names.get(k, k):10} {fmt_n[k]:5} tex  {v/1e6:8.1f} MB')
print('by largest side:', {k: f'{v/1e6:.0f} MB' for k, v in sizes.items()})
print('mipmapped share: %.0f%%' % (100 * sum(b[0] for b in big if b[4] > 1) / total))
for b in sorted(big, reverse=True)[:10]: print(f'  {b[0]/1e6:6.1f} MB {b[1]}x{b[2]} {names.get(b[3], b[3])} mips={b[4]} {b[5]} ({b[6]})')
