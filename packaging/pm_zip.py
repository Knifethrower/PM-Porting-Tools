#!/usr/bin/env python3
"""Build a PortMaster port zip from a release folder.

Release folder (the layout every port in C:\\Claude uses):
  <release>/<Port Name>.sh          the launcher (exactly one .sh at the top)
  <release>/port.json, README.md, gameinfo.xml, screenshot.png, cover.png
                                    (whichever exist; they go into the port folder in the zip;
                                    testing_thread.txt stays out: it is the Discord post, not port content)
  <release>/<portdir>/              the port folder (exactly one folder at the top, or --portdir)

Zip layout (what PortMaster installs from its autoinstall folder):
  <Port Name>.sh
  <portdir>/...                     port files plus the metadata files above

Permissions: 0755 for the launcher, ELF files, files that start with '#!' and *.so / *.so.N;
0644 for everything else (no exec list to keep up to date). Text files (by extension, plus '#!'
scripts) get Unix line endings, except paths matching --keep-crlf. __pycache__ is skipped.

usage: pm_zip.py <release dir> [out.zip] [--portdir NAME] [--keep-crlf GLOB]... [--skip GLOB]... [--gamedata DIR]
  out.zip     default: <portdir>.zip next to the release folder
  --skip      leave matching paths out (PC-only builds kept in the release folder, e.g. '*.dll')
  --gamedata  personal builds only: DIR is added as <portdir>/gamedata/ unchanged (streamed, zip64)
"""
import argparse, fnmatch, os, sys, time, zipfile

META = ['port.json', 'README.md', 'gameinfo.xml', 'screenshot.png', 'cover.png']
TEXT = ('.sh', '.py', '.txt', '.ini', '.json', '.md', '.xml', '.cfg', '.patch', '.c', '.cs')


def is_exec(head, arc):
    name = os.path.basename(arc)
    return head[:4] == b'\x7fELF' or head[:2] == b'#!' or name.endswith('.so') or '.so.' in name


def entry(arc, src, mode):
    info = zipfile.ZipInfo(arc, time.localtime(os.path.getmtime(src))[:6])
    info.external_attr = (0o100000 | mode) << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    return info


def add(z, src, arc, keep_crlf):
    if arc.endswith('/'):                                # empty folder (e.g. assets/ for the game files)
        info = zipfile.ZipInfo(arc, time.localtime(os.path.getmtime(src))[:6])
        info.external_attr = (0o040755 << 16) | 0x10
        z.writestr(info, b'')
        return
    with open(src, 'rb') as f:
        data = f.read()
    mode = 0o755 if is_exec(data[:4], arc) else 0o644
    if (arc.endswith(TEXT) or data[:2] == b'#!') and not any(fnmatch.fnmatch(arc, g) for g in keep_crlf):
        data = data.replace(b'\r\n', b'\n')              # scripts must have Unix line endings
    z.writestr(entry(arc, src, mode), data, compresslevel=9)


def add_stream(z, src, arc):
    with open(src, 'rb') as f:
        mode = 0o755 if is_exec(f.read(4), arc) else 0o644
        f.seek(0)
        with z.open(entry(arc, src, mode), 'w', force_zip64=True) as out:
            while chunk := f.read(1 << 20):
                out.write(chunk)


def walk(base):
    for root, dirs, files in os.walk(base):
        dirs[:] = sorted(d for d in dirs if d != '__pycache__')
        if not dirs and not files and root != base:
            yield root, os.path.relpath(root, base).replace(os.sep, '/') + '/'
        for fn in sorted(files):
            p = os.path.join(root, fn)
            yield p, os.path.relpath(p, base).replace(os.sep, '/')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('release')
    ap.add_argument('out', nargs='?')
    ap.add_argument('--portdir')
    ap.add_argument('--keep-crlf', action='append', default=[], metavar='GLOB')
    ap.add_argument('--skip', action='append', default=[], metavar='GLOB')
    ap.add_argument('--gamedata')
    a = ap.parse_args()

    rel = os.path.abspath(a.release)
    top = sorted(os.listdir(rel))
    launchers = [f for f in top if f.endswith('.sh') and os.path.isfile(os.path.join(rel, f))]
    dirs = [d for d in top if os.path.isdir(os.path.join(rel, d))]
    if len(launchers) != 1:
        sys.exit(f'need exactly one launcher .sh in {rel}, found {launchers}')
    portdir = a.portdir or (dirs[0] if len(dirs) == 1 else None)
    if not portdir:
        sys.exit(f'several folders in {rel} ({dirs}): pass --portdir')
    out = a.out or os.path.join(os.path.dirname(rel), portdir + '.zip')

    with zipfile.ZipFile(out, 'w') as z:
        add(z, os.path.join(rel, launchers[0]), launchers[0], a.keep_crlf)
        for m in META:
            if os.path.exists(os.path.join(rel, m)):
                add(z, os.path.join(rel, m), f'{portdir}/{m}', a.keep_crlf)
        for p, r in walk(os.path.join(rel, portdir)):
            if not any(fnmatch.fnmatch(f'{portdir}/{r}', g) for g in a.skip):
                add(z, p, f'{portdir}/{r}', a.keep_crlf)
        if a.gamedata:                            # personal build: prepared game data included
            for p, r in walk(a.gamedata):
                add_stream(z, p, f'{portdir}/gamedata/{r}')
    print(f'{out}: {os.path.getsize(out) / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
