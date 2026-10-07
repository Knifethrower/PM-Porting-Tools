#!/usr/bin/env python3
"""Compare two piglit result folders: tests that pass on the reference but not on gl4es, grouped by area.
usage: piglit_diff.py REF_RESULTS GL4ES_RESULTS"""
import bz2, collections, gzip, json, os, sys


def load(d):
    f = [x for x in os.listdir(d) if x.startswith('results.json')][0]
    op = bz2.open if f.endswith('bz2') else gzip.open if f.endswith('gz') else open
    return json.load(op(os.path.join(d, f), 'rt'))['tests']


def area(name):
    p = name.split('@')
    if len(p) > 2 and p[0] == 'spec':
        return '@'.join(p[:2]) if p[1] not in ('!opengl 1.1', '!opengl 1.0', '!opengl 2.0', '!opengl 2.1', 'glsl-1.10', 'glsl-1.20') else '@'.join(p[:3])
    return '@'.join(p[:2])


def main():
    ref, got = load(sys.argv[1]), load(sys.argv[2])
    rows = collections.defaultdict(list)
    st = collections.Counter()
    for n, t in ref.items():
        g = got.get(n)
        if not g:
            continue
        a, b = t['result'], g['result']
        st[(a, b)] += 1
        if a == 'pass' and b != 'pass':
            msg = ''
            for line in (g.get('out') or '').splitlines() + (g.get('err') or '').splitlines():
                if any(k in line.lower() for k in ('fail', 'error', 'expected', 'probe', 'abort', 'segv', 'sanitizer')):
                    msg = line.strip()[:160]; break
            rows[area(n)].append((n, b, msg))
    print('# piglit: passes on Mesa, not through gl4es\n')
    print('| reference | gl4es | tests |\n|--|--|--|')
    for (a, b), c in sorted(st.items(), key=lambda x: -x[1]):
        print(f'| {a} | {b} | {c} |')
    print('\n| area | failing |\n|--|--|')
    for k in sorted(rows, key=lambda k: -len(rows[k])):
        print(f'| {k} | {len(rows[k])} |')
    for k in sorted(rows, key=lambda k: -len(rows[k])):
        print(f'\n## {k}\n')
        for n, b, msg in sorted(rows[k]):
            print(f'- `{n}` {b} {msg}')


if __name__ == '__main__':
    main()
