#!/usr/bin/env python3
"""Quick look at results-<variant>.json: status counts per group and the most common failure reasons.
usage: summarize.py RESULTS_JSON [group-prefix] [n examples]"""
import collections, json, re, sys

res = json.load(open(sys.argv[1]))
pfx = sys.argv[2] if len(sys.argv) > 2 else ''
nex = int(sys.argv[3]) if len(sys.argv) > 3 else 3
groups = collections.defaultdict(collections.Counter)
reasons = collections.defaultdict(list)
for n, e in res.items():
    if not n.startswith(pfx):
        continue
    g = 'corpus/' + n.split('/')[1].split('_')[0] + '/' + n.split('/')[-1] if n.startswith('corpus/') else '/'.join(n.split('/')[:2])
    groups[g][e['status']] += 1
    if e['status'] not in ('pass', 'skip', 'ref-rejected'):
        why = (e.get('progerr') or [''])[0] or '; '.join(e['why']) or ','.join(e.get('sanitizer', []))
        why = re.sub(r'\d+(\.\d+)?%', 'N%', why)
        why = re.sub(r'pos=\d+', 'pos=N', why)
        why = re.sub(r'\b[\w.\[\]-]+\.\d+\.rgba', 'IMG', why)
        why = re.sub(r'\(max \d+', '(max N', why)
        why = re.sub(r'e\.g\. \[\d+\] ref [-\d.e]+ got [-\d.e]+', 'e.g. ...', why)
        reasons[(e['status'], why[:150])].append(n)
for g in sorted(groups):
    print(f'{g:28s} ' + ' '.join(f'{k}={v}' for k, v in groups[g].most_common()))
print()
for (st, why), ns in sorted(reasons.items(), key=lambda x: -len(x[1]))[:40]:
    print(f'{len(ns):5d} {st:9s} {why}')
    for n in ns[:nex]:
        print(f'          {n}')
