"""Give a Windows Unity 4.5 mainData a Linux-layout PlayerSettings object.

Unity 4 release builds have no type trees and PlayerSettings is serialized with platform-specific
fields, so a Linux player misreads the Windows object ("Mismatched serialization in the builtin
class 'PlayerSettings'"). This takes the PlayerSettings object from a Linux build of the same
Unity 4.5 family (the donor) and puts the game's own company / product / bundle identifier strings
and default resolution into it, and disables the resolution dialog. Other fields keep the donor's
values. Pure Python (unity4file.py), no UnityPy needed.

Usage: python fix_playersettings.py <game mainData> <donor Linux mainData> [out]
"""
import shutil, struct, sys
from unity4file import Unity4File

PLAYER_SETTINGS = 129


def ps_raw(path):
    with Unity4File(path) as uf:
        i, = uf.find(PLAYER_SETTINGS)
        return i, bytes(uf.raw(i))


def read_str(b, p):
    n, = struct.unpack_from('<i', b, p)
    s = b[p + 4:p + 4 + n]
    return s, (p + 4 + n + 3) & ~3


def pack_str(s):
    b = struct.pack('<i', len(s)) + s
    return b + b'\0' * ((-len(b)) % 4)


def split(b):
    """-> head(24 bytes), company, product, middle, bundle id, tail (the int after the bundle id)."""
    company, p = read_str(b, 24)
    product, p = read_str(b, p)
    # bundle identifier is the last string field: find it from the end
    for q in range(len(b) - 8, p, -4):
        n, = struct.unpack_from('<i', b, q)
        end = (q + 4 + n + 3) & ~3
        if 0 < n < 256 and end + 4 == len(b) and b[q + 4:q + 4 + n].startswith(b'com.'):
            return b[:24], company, product, b[p:q], b[q + 4:q + 4 + n], b[end:]
    raise ValueError('bundle identifier not found')


def main():
    game_path, donor_path = sys.argv[1], sys.argv[2]
    out_path = sys.argv[3] if len(sys.argv) > 3 else game_path
    gi, g = ps_raw(game_path)
    _, d = ps_raw(donor_path)
    _, gcomp, gprod, gmid, gbundle, _ = split(g)
    dhead, _, _, dmid, _, dtail = split(d)
    mid = bytearray(dmid)
    mid[16:24] = gmid[16:24]                                   # default screen width / height
    # displayResolutionDialog (0 = disabled) follows the two null PPtrs after the rendering
    # settings: 16 bytes (cursor), 4 ints (screen / web sizes), 4 ints, 2 PPtr halves -> +56.
    # The dialog (ScreenSelector) crashes on some X servers and is useless on a handheld.
    assert struct.unpack_from('<2i', mid, 48) == (-1, -1), 'unexpected layout'
    struct.pack_into('<i', mid, 56, 0)
    new = dhead + pack_str(gcomp) + pack_str(gprod) + bytes(mid) + pack_str(gbundle) + dtail
    if out_path != game_path:
        shutil.copyfile(game_path, out_path)
    with Unity4File(out_path) as uf:
        uf.rewrite({gi: new})
    print(f'PlayerSettings: {len(g)} -> {len(new)} bytes; company={gcomp.decode()} product={gprod.decode()} '
          f'bundle={gbundle.decode()} default res={struct.unpack_from("<2i", mid, 16)}')


if __name__ == '__main__':
    main()
