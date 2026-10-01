"""Compress Usagi Yojimbo's uncompressed textures to DXT (Unity 4.5 data, in place, low memory).

The game ships 1.1 GB of uncompressed ARGB32 textures, almost all uniSWF comic pages. On the
handheld the game runs on gl4es, which only advertises DXT (S3TC): it decodes DXT on the CPU at
load and keeps the texture as 16-bit colour (or 32-bit with LIBGL_AVOID16BITS=1). Unity 4.5's
desktop player has no use for ASTC, so DXT is the format that works on both the PC and the device.

  * ARGB32 / RGBA32 / RGB24, not readable, no mipmaps, sides a multiple of 4 and >= 16
  * fully opaque -> DXT1 (4 bpp), otherwise DXT5 (8 bpp); pixel rows stay in Unity's bottom-up
    order, which is also the order of the DXT blocks
  * comic lettering pages (JUSTTEXT) and other text textures stay uncompressed so text is crisp

Works on the files directly so it fits a 2 GB handheld (unity4file.py): the assets file is
memory-mapped, one texture is converted at a time and the new file is streamed to disk.
Texture2D objects (class 28) are parsed with the Unity 4.5 layout. Needs only etcpak.

Usage: python textures_dxt.py <Data dir> [--dry-run]
"""
import glob, os, re, struct, sys
import etcpak
from unity4file import Unity4File

TEXTURE2D = 28
FMT_RGB24, FMT_RGBA32, FMT_ARGB32, FMT_DXT1, FMT_DXT5 = 3, 4, 5, 10, 12
KEEP_RE = re.compile(rb'(?i)justtext|(?<!no)text(?!ure)|font')
MIN_SIDE = 16


def parse_texture(raw):
    """-> dict with the fields we need and their offsets, or None if the layout doesn't fit."""
    p = 0
    nlen, = struct.unpack_from('<i', raw, p); name = bytes(raw[4:4 + nlen]); p = (4 + nlen + 3) & ~3
    w, h, complete, fmt = struct.unpack_from('<iiii', raw, p); fmt_off = p + 12; complete_off = p + 8; p += 16
    mip, readable = raw[p], raw[p + 1]; p = (p + 3 + 3) & ~3                 # 3 bools, aligned
    p += 4 * 2 + 4 * 4 + 4 * 2                                                 # count, dim, settings, lightmap, colorspace
    size, = struct.unpack_from('<i', raw, p)
    data_off = p + 4
    if size != complete or data_off + size > len(raw):
        return None
    return dict(name=name, w=w, h=h, fmt=fmt, mip=mip, readable=readable, fmt_off=fmt_off,
                complete_off=complete_off, size_off=p, data_off=data_off, size=size)


def convertible(t):
    return (t['fmt'] in (FMT_ARGB32, FMT_RGBA32, FMT_RGB24) and not t['readable'] and not t['mip']
            and t['w'] % 4 == 0 and t['h'] % 4 == 0 and min(t['w'], t['h']) >= MIN_SIDE
            and not KEEP_RE.search(t['name']))


def to_rgba(src, fmt, n):
    """Unity pixel data -> RGBA bytes (n pixels)."""
    out = bytearray(n * 4)
    if fmt == FMT_ARGB32:
        out[0::4], out[1::4], out[2::4], out[3::4] = src[1::4], src[2::4], src[3::4], src[0::4]
    elif fmt == FMT_RGBA32:
        out[:] = src
    else:                                                                      # RGB24
        out[0::4], out[1::4], out[2::4] = src[0::3], src[1::3], src[2::3]
        out[3::4] = b'\xff' * n
    return out


def encode(t, pixels):
    rgba = to_rgba(pixels, t['fmt'], t['w'] * t['h'])
    if min(rgba[3::4]) == 255:
        return FMT_DXT1, etcpak.compress_bc1(bytes(rgba), t['w'], t['h'])
    return FMT_DXT5, etcpak.compress_bc3(bytes(rgba), t['w'], t['h'])


def convert_file(path, dry_run=False):
    try:
        uf = Unity4File(path)
    except ValueError:
        return 0, 0, 0
    with uf:
        new, size_in, size_out = {}, 0, 0
        for i in uf.find(TEXTURE2D):
            raw = uf.raw(i)
            t = parse_texture(raw)
            if not t or not convertible(t):
                continue
            fmt, blob = encode(t, raw[t['data_off']:t['data_off'] + t['size']])
            obj = bytearray(raw[:t['size_off']])
            struct.pack_into('<i', obj, t['fmt_off'], fmt)
            struct.pack_into('<i', obj, t['complete_off'], len(blob))
            obj += struct.pack('<i', len(blob)) + blob + raw[t['data_off'] + t['size']:]
            new[i] = bytes(obj)
            size_in += t['size']; size_out += len(blob)
        if new and not dry_run:
            uf.rewrite(new)
    return len(new), size_in, size_out


def main():
    data_dir = sys.argv[1]
    dry = '--dry-run' in sys.argv
    files = [os.path.join(data_dir, 'mainData'), os.path.join(data_dir, 'resources.assets')] + \
        sorted(glob.glob(os.path.join(data_dir, 'sharedassets*.assets'))) + \
        sorted(glob.glob(os.path.join(data_dir, 'level*')), key=lambda p: int(re.sub(r'\D', '', os.path.basename(p)) or 0))
    tn = ti = to = 0
    for f in files:
        n, si, so = convert_file(f, dry)
        if n:
            print(f'{os.path.basename(f)}: {n} textures, {si >> 20} -> {so >> 20} MB', flush=True)
        tn += n; ti += si; to += so
    print(f'total: {tn} textures, {ti >> 20} -> {to >> 20} MB{" (dry run)" if dry else ""}')


if __name__ == '__main__':
    main()
