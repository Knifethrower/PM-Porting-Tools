#!/usr/bin/env python3
"""Check a port's release folder (or its zip) against the PortMaster packaging rules.

Rules from PortMaster-New's contribution docs (AGENTS.md and the packaging guide): port.json v4
fields, the launcher's boilerplate header and banned patterns, folder naming, licenses, README
shape, gameinfo.xml paths, screenshot/cover size, LF line endings, no em dashes. Started as Space
Trader's package_port.py checks.

usage: pm_lint.py <release dir | port zip>
  release dir: <Port Name>.sh, port.json, README.md, gameinfo.xml, screenshot.png at the top, plus
               one port folder (the layout pm_zip.py packs). A zip made by pm_zip.py works too:
               it is unpacked to a temp folder and the metadata is looked up in the port folder.
Exit status 1 when any ERROR is found; WARN lines are things a reviewer will probably ask about.
"""
import json, os, re, struct, sys, tempfile, zipfile

EM_DASH = '\u2014'
BOILERPLATE = '''#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
'''
ATTR_KEYS = ('title', 'porter', 'desc', 'desc_md', 'inst', 'inst_md', 'genres', 'image', 'rtr', 'exp',
             'runtime', 'store', 'availability', 'reqs', 'arch', 'min_glibc')
BANNED = (('SDL_VIDEODRIVER=', 'never force a video driver in the launcher'),
          ('SDL_AUDIODRIVER=', 'never force an audio driver in the launcher'),
          ('export LD_PRELOAD', 'never export LD_PRELOAD (leaks into gptokeyb); set it on the game command only'),
          ('"$GPTOKEYB2"', 'do not quote $GPTOKEYB2 (it can carry an env prefix)'),
          ('"$GPTOKEYB"', 'do not quote $GPTOKEYB (it can carry an env prefix)'))
TEXT = ('.sh', '.ini', '.json', '.cfg', '.py')          # port scripts and configs (game data is left alone)

errors, warns = [], []
err = errors.append
warn = warns.append


def read(path):
    return open(path, encoding='utf-8', errors='replace').read()


def png_size(path):
    head = open(path, 'rb').read(24)
    if head[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    return struct.unpack('>II', head[16:24])


def lint(rel):
    top = sorted(os.listdir(rel))
    launchers = [f for f in top if f.endswith('.sh') and os.path.isfile(os.path.join(rel, f))]
    dirs = [d for d in top if os.path.isdir(os.path.join(rel, d))]
    if len(launchers) != 1:
        err(f'need exactly one launcher .sh at the top, found {launchers}')
        return
    if len(dirs) != 1:
        err(f'need exactly one port folder at the top, found {dirs}')
        return
    sh, portdir = launchers[0], dirs[0]
    P = os.path.join(rel, portdir)

    def meta(name):                       # release folder: at the top; pm_zip.py zip: in the port folder
        for p in (os.path.join(rel, name), os.path.join(P, name)):
            if os.path.isfile(p):
                return p
        return None

    # ---- files ----
    for name in ('port.json', 'README.md', 'gameinfo.xml', 'screenshot.png'):
        if not meta(name):
            err('missing ' + name)
    if not meta('cover.png'):
        warn('no cover.png (optional, but expected for a real submission)')
    if not meta('testing_thread.txt'):
        warn('no testing_thread.txt (the Discord testing-thread post)')

    # ---- naming and layout ----
    if not re.fullmatch(r'[a-z0-9]+', portdir):
        err(f'port folder "{portdir}" must be lowercase and squashed (a-z, 0-9 only)')
    lic = os.path.join(P, 'licenses')
    if not os.path.isdir(lic) or not [f for f in os.listdir(lic) if os.path.getsize(os.path.join(lic, f))]:
        err(f'{portdir}/licenses/ missing or empty (one LICENSE file per bundled component)')
    gptk = [os.path.relpath(os.path.join(r, f), rel) for r, _, fs in os.walk(P) for f in fs if f.endswith('.gptk')]
    if gptk:
        warn(f'classic .gptk file(s) {gptk}: new ports use a gptokeyb2 .ini')

    # ---- every text file: LF endings; generated content: no em dashes ----
    generated = [os.path.join(rel, sh)] + [m for m in map(meta, ('port.json', 'README.md', 'gameinfo.xml', 'testing_thread.txt')) if m]
    for root, ds, fs in os.walk(rel):
        ds[:] = [d for d in ds if d not in ('gamedata', '__pycache__', 'licenses')]
        for f in fs:
            p = os.path.join(root, f)
            if f.endswith(TEXT) and b'\r\n' in open(p, 'rb').read():
                err(f'CRLF line endings in {os.path.relpath(p, rel)}')
            if f.endswith('.ini') and os.path.dirname(p) == P:
                generated.append(p)
    for p in generated:
        if EM_DASH in read(p):
            err(f'em dash in {os.path.relpath(p, rel)}')

    # ---- launcher ----
    s = read(os.path.join(rel, sh))
    if not s.replace('\r\n', '\n').startswith(BOILERPLATE):
        err('launcher must start with the standard boilerplate header, unchanged (reference 1.7.1)')
    for pat, why in BANNED:
        for line in s.splitlines():
            if pat in line and not line.lstrip().startswith('#'):
                if 'SDL_VIDEODRIVER=sdl2' in line:   # SDL3-on-SDL2 backend selector, not a device driver
                    warn(f'launcher sets SDL_VIDEODRIVER=sdl2: fine only for SDL3 on the SDL2 backend: {line.strip()}')
                else:
                    err(f'launcher: {why}: {line.strip()}')
                break
    if '$GPTOKEYB2' not in s:
        warn('launcher does not use $GPTOKEYB2 (new ports use gptokeyb2)')
    if 'pm_finish' not in s:
        warn('launcher never calls pm_finish')

    # ---- port.json ----
    if meta('port.json'):
        try:
            pj = json.loads(read(meta('port.json')))
        except ValueError as e:
            err(f'port.json is not valid JSON: {e}')
            pj = {}
        attr = pj.get('attr', {})
        if pj.get('version') != 4:
            err('port.json version must be 4')
        if pj.get('name') != portdir + '.zip':
            err(f'port.json name must be "{portdir}.zip", got {pj.get("name")!r}')
        if sorted(pj.get('items', [])) != sorted([sh, portdir]) and sorted(pj.get('items', [])) != sorted([sh, portdir + '/']):
            err(f'port.json items must be exactly the launcher and the port folder: {[sh, portdir]}, got {pj.get("items")}')
        for k in ATTR_KEYS:
            if k not in attr:
                err('port.json attr missing: ' + k)
        porter = attr.get('porter')
        if not isinstance(porter, list) or not porter or any(not p or p.lower() in ('yourhandle', 'todo', 'tbd') for p in porter):
            err('port.json porter must be a list of real handles')
        for k in ('runtime', 'store', 'genres', 'reqs', 'arch'):
            if k in attr and not isinstance(attr[k], list):
                err(f'port.json attr {k} must be a list')
        for r in attr.get('runtime') or []:
            if r.endswith('.squashfs'):
                err(f'port.json runtime must be the bare catalog key, got {r}')
        if attr.get('exp') is not False:
            warn('port.json exp is not false')
        for k in ('desc', 'inst'):
            v = attr.get(k) or ''
            if '\n' in v or '\\n' in v or re.search(r'`|\*\*|\]\(|^#', v):
                err(f'port.json {k} must be plain single-paragraph text (markdown goes in {k}_md)')
        if attr.get('title') and attr['title'] + '.sh' != sh:
            warn(f'port.json title "{attr["title"]}" does not match the launcher name "{sh}"')

    # ---- README ----
    if meta('README.md'):
        r = read(meta('README.md'))
        if not r.startswith('## Notes'):
            err('README.md must start directly at "## Notes" (no title above it)')
        if re.search(r'known limitations', r, re.I):
            err('README.md must not have a "Known Limitations" section')
        if re.search(r'\|\s*unused\s*\|', r, re.I):
            err('README.md controls table has an "Unused" row (leave unbound buttons out)')
        if '## Controls' not in r:
            err('README.md has no "## Controls" section')
        if not re.search(r'^## (Compile|Build)', r, re.M):
            warn('README.md has no "## Compile" / "## Build" section')

    # ---- gameinfo.xml ----
    if meta('gameinfo.xml'):
        g = read(meta('gameinfo.xml'))
        if f'<path>./{sh}</path>' not in g:
            err(f'gameinfo.xml <path> must be "./{sh}"')
        m = re.search(r'<image>\./([^<]*)</image>', g)
        if not m:
            err('gameinfo.xml has no <image>./<portdir>/...</image>')
        elif not m.group(1).startswith(portdir + '/'):
            err(f'gameinfo.xml <image> must carry the port folder prefix: ./{portdir}/...')
        elif not meta(m.group(1).split('/', 1)[1]):
            err(f'gameinfo.xml <image> points at a file that does not exist: {m.group(1)}')
        for tag in ('name', 'desc', 'releasedate', 'developer', 'publisher', 'genre'):
            if f'<{tag}>' not in g:
                err(f'gameinfo.xml missing <{tag}>')
        d = re.search(r'<releasedate>([^<]*)</releasedate>', g)
        if d and not re.fullmatch(r'\d{8}T000000', d.group(1)):
            err('gameinfo.xml releasedate must be YYYYMMDDT000000')

    # ---- screenshot / cover ----
    for img in ('screenshot.png', 'cover.png'):
        p = meta(img)
        if p:
            size = png_size(p)
            if not size:
                err(img + ' is not a PNG')
            elif img == 'screenshot.png':
                w, h = size
                if w < 640 or h < 480 or abs(w / h - 4 / 3) > 0.01:
                    err(f'screenshot.png must be 4:3 and at least 640x480 (is {w}x{h})')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    target = sys.argv[1]
    if zipfile.is_zipfile(target):
        with tempfile.TemporaryDirectory() as t:
            zipfile.ZipFile(target).extractall(t)
            lint(t)
    else:
        lint(target)
    for w in warns:
        print('WARN  ' + w)
    for e in errors:
        print('ERROR ' + e)
    print(f'{len(errors)} errors, {len(warns)} warnings')
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()
