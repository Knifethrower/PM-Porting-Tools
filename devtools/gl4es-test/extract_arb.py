#!/usr/bin/env python3
"""Pull every ARB assembly program out of game files (Unity 4 keeps its "opengl" shaders as ARB text in the
Shader objects of *.assets / mainData / *.unity3d; any other binary works too) for the corpus/ tests.

  extract_arb.py OUTDIR TAG PATH...      (PATH: files or folders, searched recursively)

Writes OUTDIR/<TAG>_<sha1[:10]>.vp|.fp, one per distinct program, and prints counts. Programs are cut from
"!!ARBvp1.0"/"!!ARBfp1.0" to the first "END" token. Escaped newlines (\\n inside quoted strings) are unescaped."""
import hashlib, os, re, sys

PROG = re.compile(rb'!!ARB(vp|fp)1\.0(?:(?!!!ARB).){0,200000}?\bEND\b', re.S)


def programs(data):
    for m in PROG.finditer(data):
        text = m.group(0)
        if b'\\n' in text and b'\n' not in text:
            text = text.replace(b'\\n', b'\n').replace(b'\\"', b'"')
        yield m.group(1).decode(), text.decode('latin-1') + '\n'


def main():
    out, tag, paths = sys.argv[1], sys.argv[2], sys.argv[3:]
    os.makedirs(out, exist_ok=True)
    seen = set(); counts = {'vp': 0, 'fp': 0}; files = 0
    for p in paths:
        walk = [(p, [], [os.path.basename(p)])] if os.path.isfile(p) else os.walk(p)
        for dp, _, fs in walk:
            for f in fs:
                fp = os.path.join(dp, f) if os.path.isdir(p) else p
                try:
                    if os.path.getsize(fp) < 64:
                        continue
                    data = open(fp, 'rb').read()
                except OSError:
                    continue
                if b'!!ARB' not in data:
                    continue
                files += 1
                for kind, text in programs(data):
                    h = hashlib.sha1(text.encode('latin-1')).hexdigest()[:10]
                    if h in seen:
                        continue
                    seen.add(h); counts[kind] += 1
                    open(os.path.join(out, f'{tag}_{h}.{kind}'), 'w', newline='\n').write(text)
    print(f'{tag}: {files} files with ARB programs, {counts["vp"]} vertex + {counts["fp"]} fragment programs (distinct)')


if __name__ == '__main__':
    main()
