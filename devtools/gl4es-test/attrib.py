#!/usr/bin/env python3
"""Fuzz attribution by single features: for every feature (and every feature prefix, e.g. 'blend-SRC_ALPHA-*'
split into its parts), the fail rate of the seeds that have it vs the base rate. Also tries "explanations":
features whose seeds explain most failures. usage: attrib.py RESULTS_JSON [prefix=ffp/] [min seeds=30]"""
import collections, json, sys

res = json.load(open(sys.argv[1]))
pfx = sys.argv[2] if len(sys.argv) > 2 else 'ffp/'
mn = int(sys.argv[3]) if len(sys.argv) > 3 else 30
seeds = {n: e for n, e in res.items() if n.startswith(pfx) and e['status'] in ('pass', 'fail', 'crash', 'timeout', 'sanitizer')}
fail = {n for n, e in seeds.items() if e['status'] != 'pass'}


def parts(f):
    out = {f}
    p = f.split('-')
    if p[0] in ('blend', 'stencil', 'combine', 'tex0', 'tex1') and len(p) > 2:
        for i in range(1, len(p)):
            out.add(f'{p[0]}[{i}]={p[i]}')
    return out


cnt, fc = collections.Counter(), collections.Counter()
for n, e in seeds.items():
    fs = set()
    for f in e['feat']:
        fs |= parts(f)
    for f in fs:
        cnt[f] += 1
        fc[f] += n in fail
base = len(fail) / max(1, len(seeds))
print(f'{len(seeds)} seeds, {len(fail)} failing ({base * 100:.1f}%)\n')
rows = sorted(((fc[f] / c, f, fc[f], c) for f, c in cnt.items() if c >= mn), reverse=True)
print('| feature | fail rate | failing / with |\n|--|--|--|')
for r, f, k, c in rows[:30]:
    print(f'| {f} | {r * 100:.0f}% | {k}/{c} |')
print('\nlowest fail rates (features that look safe):')
for r, f, k, c in sorted(rows)[:10]:
    print(f'  {f}: {r * 100:.0f}% ({k}/{c})')
# greedy cover: which features explain the failures
left = set(fail); print('\nfeatures covering the failures (greedy, fail rate >= 2x base):')
for _ in range(12):
    best = None
    for f, c in cnt.items():
        if c < mn or fc[f] / c < 2 * base:
            continue
        k = sum(1 for n in left if f in set().union(*[parts(x) for x in seeds[n]['feat']]))
        if not best or k > best[1]:
            best = (f, k)
    if not best or best[1] == 0:
        break
    left = {n for n in left if best[0] not in set().union(*[parts(x) for x in seeds[n]['feat']])}
    print(f'  {best[0]}: explains {best[1]}, {len(left)} left')
