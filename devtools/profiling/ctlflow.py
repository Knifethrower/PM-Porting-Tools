"""Break down control-flow samples (CALL/JMP/RET/Jcc) by instruction form and module.
usage: python ctlflow.py <profile dir> <tid>"""
import bisect, collections, re, sys
d, tid = sys.argv[1], int(sys.argv[2])
ent = {}
for line in open(f'{d}/perfmap.txt', errors='replace'):
    p = line.rstrip('\n').split(' ', 2)
    if len(p) < 3: continue
    desc, _, mnem = p[2].rpartition(':')
    ent[int(p[0], 16)] = (int(p[1]), mnem.strip(), desc)
keys = sorted(ent)
def module(desc):
    if desc.startswith('0x'): return 'JIT'
    m = re.search(r'([^/ ]+?\.(?:so(?:\.[0-9]+)*|x86_64))', desc)
    return m.group(1) if m else desc.split()[0]
tot = 0; form = collections.Counter(); site = collections.Counter(); site_desc = {}
for line in open(f'{d}/samples.txt'):
    t, ip, c = line.split()
    if int(t) != tid: continue
    ip, c = int(ip, 16), int(c); tot += c
    i = bisect.bisect_right(keys, ip) - 1
    if i < 0: continue
    n, mnem, desc = ent[keys[i]]
    if not (keys[i] <= ip < keys[i] + 4 * max(n, 1)): continue
    op = mnem.split()[0] if mnem else ''
    if op in ('CALL', 'JMP', 'RET', 'RETN') or op.startswith('J') and op != 'JMP':
        kind = mnem if op in ('CALL', 'JMP', 'RET', 'RETN') else 'Jcc'
        form[(module(desc), kind, n)] += c
        site[keys[i]] += c; site_desc[keys[i]] = (mnem, desc, n)
cf = sum(form.values())
print(f'tid {tid}: {tot} samples, control flow {cf} ({100*cf/tot:.1f}%)')
print(f'{"module":<14} {"instruction form":<22} {"ARM":>4} {"share of thread":>16}')
for (m, k, n), c in form.most_common(25):
    print(f'{m:<14} {k:<22} {n:>4} {100*c/tot:15.2f}%')
print('\nhottest control-flow sites:')
for a, c in site.most_common(20):
    mnem, desc, n = site_desc[a]
    print(f'  {100*c/tot:5.2f}%  {mnem:<12} ARM {n:<3} {desc[:70]}')
