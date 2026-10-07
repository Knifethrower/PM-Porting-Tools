#!/usr/bin/env python3
"""Groups the AddressSanitizer / UBSan reports of an asan variant's run by error kind + first gl4es frame.
usage: asan_summary.py RESULTDIR VARIANT"""
import collections, os, re, sys

d = os.path.join(sys.argv[1], sys.argv[2])
groups = collections.defaultdict(list)
for f in sorted(os.listdir(d)):
    if not f.endswith('.err'):
        continue
    txt = open(os.path.join(d, f), errors='replace').read()
    for m in re.finditer(r'(ERROR: AddressSanitizer: [\w-]+|runtime error: [^\n]*)', txt):
        kind = m.group(1)
        tail = txt[m.end():m.end() + 4000]
        frame = re.search(r'#\d+ 0x[0-9a-f]+ in (\w+) (/[^\s]*src/gl[^\s:]*:\d+)', tail)
        where = f'{frame.group(1)} {os.path.basename(frame.group(2))}' if frame else '?'
        if kind.startswith('runtime error'):
            k2 = re.sub(r'-?\d+', 'N', kind)[:90]
            key = (k2, re.search(r'src/gl/[\w.]+:\d+', txt[max(0, m.start() - 200):m.start()]).group(0) if re.search(r'src/gl/[\w.]+:\d+', txt[max(0, m.start() - 200):m.start()]) else where)
        else:
            key = (kind, where)
        groups[key].append(f[:-4])
for (kind, where), tests in sorted(groups.items(), key=lambda x: -len(x[1])):
    print(f'{len(tests):5d}  {kind}  @ {where}')
    print(f'       e.g. {", ".join(sorted(set(tests))[:4])}')
