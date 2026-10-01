"""Per-thread breakdown of samples by x86 instruction (mnemonic) with box64's ARM expansion.
usage: python opstats.py <profile dir> [tid ...]"""
import bisect, collections, re, sys
d = sys.argv[1]
ent = {}
for line in open(f'{d}/perfmap.txt', errors='replace'):
    p = line.rstrip('\n').split(' ', 2)
    if len(p) < 3: continue
    desc, _, mnem = p[2].rpartition(':')
    ent[int(p[0], 16)] = (int(p[1]), mnem.strip(), desc)
keys = sorted(ent)
threads = [int(t) for t in sys.argv[2:]]
samples = collections.defaultdict(list)
for line in open(f'{d}/samples.txt'):
    t, ip, c = line.split()
    samples[int(t)].append((int(ip, 16), int(c)))
if not threads:
    threads = sorted(samples, key=lambda t: -sum(c for _, c in samples[t]))[:4]
def module(desc):
    if desc.startswith('0x'): return 'JIT'
    m = re.search(r'([^/ ]+?\.(?:so(?:\.[0-9]+)*|x86_64))', desc)
    return m.group(1) if m else desc.split()[0]
for tid in threads:
    tot = sum(c for _, c in samples[tid]) or 1
    by_op = collections.Counter(); arm = collections.Counter(); hit = 0
    by_mod_op = collections.Counter()
    for ip, c in samples[tid]:
        i = bisect.bisect_right(keys, ip) - 1
        if i < 0: continue
        n, mnem, desc = ent[keys[i]]
        if not (keys[i] <= ip < keys[i] + 4 * max(n, 1)): continue
        op = mnem.split()[0] if mnem else '?'
        by_op[op] += c; arm[op] = n; hit += c
        by_mod_op[(module(desc), op)] += c
    print(f'\n=== tid {tid}: {tot} samples, {100*hit/tot:.0f}% in mapped x86 code ===')
    print('  x86 op        share   ARM insns (last seen)')
    for op, c in by_op.most_common(25):
        print(f'  {op:<12} {100*c/tot:5.1f}%   {arm[op]}')
    print('  -- by module/op --')
    for (m, op), c in by_mod_op.most_common(12):
        print(f'  {100*c/tot:5.1f}%  {m:<16} {op}')
