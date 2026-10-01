#!/usr/bin/env python3
"""Replace one file (usually the launcher) inside an existing port zip, keeping every other entry
and its permissions as they are: a quick launcher fix without rebuilding a big zip.

usage: repack_launcher.py <port.zip> <new file> [name in zip]
  name in zip defaults to the new file's basename (the launcher sits at the top of the zip).
  CRLF becomes LF; the replaced entry keeps its old mode (0755 for the launcher).
"""
import os, shutil, sys, zipfile

if len(sys.argv) not in (3, 4):
    sys.exit(__doc__)
src, new = sys.argv[1], sys.argv[2]
arc = sys.argv[3] if len(sys.argv) > 3 else os.path.basename(new)
data = open(new, 'rb').read().replace(b'\r\n', b'\n')
tmp = src + '.tmp'
found = False
with zipfile.ZipFile(src) as zin, zipfile.ZipFile(tmp, 'w') as zout:
    for info in zin.infolist():
        body = zin.read(info)
        if info.filename == arc:
            body, found = data, True
        zout.writestr(info, body, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
if not found:
    os.remove(tmp)
    sys.exit(f'{arc} is not in {src}')
shutil.move(tmp, src)
print(f'{src}: {arc} replaced, {len(data)} bytes')
