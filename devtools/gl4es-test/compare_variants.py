#!/usr/bin/env python3
"""Side-by-side pass counts per test group for several variants' results-<variant>.json, plus the tests that
pass in one variant and not in another. usage: compare_variants.py A.json[=label] B.json[=label] ..."""
import collections, json, sys


def group(n):
    p = n.split('/')
    if p[0] == 'corpus':
        return 'corpus/' + p[-1]   # fp / vp
    if p[0] in ('arb', 'state', 'fbo', 'glsl'):
        return '/'.join(p[:2]) if p[0] == 'arb' else p[0]
    return p[0]


runs = []
for a in sys.argv[1:]:
    path, _, label = a.partition('=')
    runs.append((label or path.rsplit('results-', 1)[-1][:-5], json.load(open(path))))
tests = set.intersection(*(set(r) for _, r in runs))
rows = collections.defaultdict(lambda: [collections.Counter() for _ in runs])
for n in tests:
    for i, (_, r) in enumerate(runs):
        rows[group(n)][i][r[n]['status']] += 1
print('| group | tests | ' + ' | '.join(f'{l} pass' for l, _ in runs) + ' | ' + ' | '.join(f'{l} crash' for l, _ in runs) + ' |')
print('|--|--|' + '--|' * (2 * len(runs)))
tot = [collections.Counter() for _ in runs]
for g in sorted(rows):
    c = rows[g]
    n = sum(c[0].values())
    for i in range(len(runs)):
        tot[i].update(c[i])
    print(f'| {g} | {n} | ' + ' | '.join(str(x['pass']) for x in c) + ' | '
          + ' | '.join(str(x['crash'] + x['timeout'] + x['sanitizer']) for x in c) + ' |')
print(f'| **all** | {len(tests)} | ' + ' | '.join(str(x["pass"]) for x in tot) + ' | '
      + ' | '.join(str(x['crash'] + x['timeout'] + x['sanitizer']) for x in tot) + ' |')
# what changes between the first and each other run
base_l, base = runs[0]
for l, r in runs[1:]:
    better = sorted(n for n in tests if base[n]['status'] != 'pass' and r[n]['status'] == 'pass')
    worse = sorted(n for n in tests if base[n]['status'] == 'pass' and r[n]['status'] != 'pass')
    print(f'\n{l} vs {base_l}: {len(better)} newly passing, {len(worse)} newly failing')
    for n in worse[:25]:
        print(f'  worse: {n} ({r[n]["status"]}: {"; ".join(r[n]["why"])[:120]})')
