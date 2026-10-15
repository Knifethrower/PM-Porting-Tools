#!/usr/bin/env python3
"""Install a port zip (and optionally the user's game files) on a handheld over its SMB share,
from Windows, then check every file's size and the MD5 of executables and libraries.

usage: install_smb.py <ports folder on the share> <port.zip> [<game files zip or folder> <subfolder>]
  e.g. install_smb.py \\\\<device ip>\\share\\roms\\ports tummybonbons.zip game_mac.zip tummybonbons/gamedata
The game files go to <ports folder>/<subfolder>. Nothing is started on the device; the user
launches the port. Knulli's guest share needs no login (\\\\<ip>\\share, ports under roms\\ports).
From Tummy Bonbons (pctest/install_cubexx.py). License: 0BSD.
"""
import hashlib, os, shutil, sys, zipfile

EXEC_HINTS = ('.so', '.aarch64', '.sh', 'box64', 'machismo')


def md5(data):
    return hashlib.md5(data).hexdigest()


def check_zip(z, root):
    bad = 0
    for i in z.infolist():
        if i.is_dir():
            continue
        f = os.path.join(root, *i.filename.split('/'))
        if not os.path.isfile(f) or os.path.getsize(f) != i.file_size:
            print('BAD size/missing', f)
            bad += 1
            continue
        name = os.path.basename(i.filename)
        if name.endswith(EXEC_HINTS) or '.so.' in name or (i.external_attr >> 16) & 0o111:
            ok = md5(z.read(i)) == md5(open(f, 'rb').read())
            print('md5 %s  %s' % ('ok ' if ok else 'BAD', i.filename))
            bad += not ok
    return bad


def main():
    if len(sys.argv) not in (3, 5):
        sys.exit(__doc__)
    ports, port_zip = sys.argv[1], sys.argv[2]
    if not os.path.isdir(ports):
        sys.exit('share not reachable: ' + ports)
    z = zipfile.ZipFile(port_zip)
    z.extractall(ports)
    bad = check_zip(z, ports)
    if len(sys.argv) == 5:
        src, dest = sys.argv[3], os.path.join(ports, *sys.argv[4].replace('\\', '/').split('/'))
        os.makedirs(dest, exist_ok=True)
        if zipfile.is_zipfile(src):
            g = zipfile.ZipFile(src)
            g.extractall(dest)
            bad += check_zip(g, dest)
        else:
            shutil.copytree(src, dest, dirs_exist_ok=True)
            print('copied', src, '->', dest)
    print('installed in', ports, '- problems:', bad)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
