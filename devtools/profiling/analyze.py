"""Summarise a nitwprof profile of Night in the Woods running under box64.

usage: python analyze.py <profile dir> [--box64 box64.debug] [--top 25]

Inputs (from the device's nightinthewoods/profile/): samples.txt, threads.txt, maps.txt,
perfmap.txt (box64 BOX64_DYNAREC_PERFMAP), optional render.tid.
box64.debug is the unstripped build of the box64 that ran (for box64's own function names).

Every sample is put in one bucket:
  x86 code run through the dynarec -> by x86 module: Unity player, libmono, Mono JIT'd C#, FMOD, ...
  box64 itself -> translating, block lookup/linking, library-call bridge, signals, interpreter, other
  native libraries -> by file (libc, Mali driver, glespass, crusty, SDL, ...)
  kernel
"""
import argparse, bisect, collections, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load_maps(path):
    maps = []
    for line in open(path):
        p = line.split()
        if len(p) < 5:
            continue
        lo, hi = (int(x, 16) for x in p[0].split('-'))
        maps.append((lo, hi, p[1], p[5] if len(p) > 5 else ''))
    maps.sort()
    return maps


def find_map(maps, starts, ip):
    i = bisect.bisect_right(starts, ip) - 1
    if i >= 0 and maps[i][0] <= ip < maps[i][1]:
        return maps[i]
    return None


def load_perfmap(path):
    """native address -> (size in bytes, x86 description). Later lines win (blocks get reused)."""
    ent = {}
    if not os.path.exists(path):
        return [], []
    with open(path, errors='replace') as f:
        for line in f:
            p = line.rstrip('\n').split(' ', 2)
            if len(p) < 3:
                continue
            try:
                addr, n = int(p[0], 16), int(p[1])
            except ValueError:
                continue
            desc = p[2].rsplit(':', 1)[0]           # drop the instruction name
            ent[addr] = (n * 4, desc)
    keys = sorted(ent)
    return keys, [ent[k] for k in keys]


def x86_module(desc):
    """'libmono.so/mono_foo + 0x10' -> ('libmono.so', 'mono_foo');
       'NITW.x86_64 + 0x1234' -> ('NITW.x86_64', '+0x1234'); '0x7f..' -> ('JIT', None)"""
    if desc.startswith('0x'):
        return 'JIT / anonymous x86 code', None
    # the ELF name may be a full path: "/roms2/.../libmono.so/mono_foo + 0x10", "/.../NITW.x86_64 + 0x12"
    m = re.match(r'^(.*?(?:\.so(?:\.[0-9]+)*|\.x86_64|box64))(?:/([^ ]+))?(?:\s*\+.*)?$', desc)
    if m:
        return os.path.basename(m.group(1)), m.group(2)
    m = re.match(r'([^ +]+)\s*\+', desc)
    if m:
        return os.path.basename(m.group(1)), None
    return desc, None


def load_box64_syms(path):
    if not path or not os.path.exists(path):
        return [], []
    cmd = ['wsl', '-e', 'aarch64-linux-gnu-nm', '-n', '-C', wslpath(path)] if os.name == 'nt' else \
          ['aarch64-linux-gnu-nm', '-n', '-C', path]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    addrs, names = [], []
    for line in out.splitlines():
        p = line.split(None, 2)
        if len(p) == 3 and p[1] in 'tTwW':
            addrs.append(int(p[0], 16)); names.append(p[2])
    return addrs, names


def wslpath(p):
    p = os.path.abspath(p).replace('\\', '/')
    return f'/mnt/{p[0].lower()}{p[2:]}' if re.match(r'^[A-Za-z]:', p) else p


BOX64_GROUPS = [
    ('box64: translating x86 code', r'native_pass|FillBlock|AddNewDynablock|dynarec_|arm64_pass|Dynablock|CreateEmptyBlock|DecodeX64|dynablock|FreeDynablock|MarkDynablock|updateNeed|getX64Address|x64emu_fork'),
    ('box64: block lookup / linking', r'DBGetBlock|DBAlternateBlock|LinkNext|getDB|JmpTable|jmptbl|getJumpTable|setJumpTable|native_next|arm64_next|CheckBlock|isJumpTableDefault|cleanDBFromAddressRange|protectDB|unprotectDB|hotpage|checkInHotPage'),
    ('box64: signals / memory protection', r'my_box64signalhandler|sigaction|signal|Protection|protect|setProtection|getProt|updateProtection|mprotect'),
    ('box64: library-call bridge', r'^my_|^my32_|wrapped|^vFE|^iFE|^pFE|^vFp|^iFp|^pFp|^[vilpfdCWuUL]F[a-zA-Z]+$|x64_resolv|NativeCall|RunFunction|GetNativeFnc'),
    ('box64: interpreter', r'^Run|^x64Run|Run64|RunF|^Test'),
    ('box64: memory / threads / other', r'.'),
]


def box64_group(name):
    for g, rx in BOX64_GROUPS:
        if re.search(rx, name):
            return g
    return BOX64_GROUPS[-1][0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('profile')
    ap.add_argument('--box64', default=os.path.join(HERE, 'box64.debug'))
    ap.add_argument('--top', type=int, default=25)
    a = ap.parse_args()
    d = a.profile
    maps = load_maps(os.path.join(d, 'maps.txt'))
    starts = [m[0] for m in maps]
    pkeys, pvals = load_perfmap(os.path.join(d, 'perfmap.txt'))
    baddrs, bnames = load_box64_syms(a.box64)
    threads = {}
    if os.path.exists(os.path.join(d, 'threads.txt')):
        for line in open(os.path.join(d, 'threads.txt')):
            t, _, n = line.strip().partition(' ')
            threads[int(t)] = n
    render_tid = None
    if os.path.exists(os.path.join(d, 'render.tid')):
        render_tid = int(open(os.path.join(d, 'render.tid')).read().split()[0])

    bucket = collections.defaultdict(collections.Counter)     # tid -> bucket -> samples
    funcs = collections.defaultdict(collections.Counter)      # tid -> function -> samples
    totals = collections.Counter()
    unmapped_dyn = 0
    for line in open(os.path.join(d, 'samples.txt')):
        t, ip, c = line.split()
        tid, ip, c = int(t), int(ip, 16), int(c)
        totals[tid] += c
        if ip >= 0xffff000000000000:
            b, fn = 'kernel', 'kernel'
        else:
            m = find_map(maps, starts, ip)
            path = m[3] if m else ''
            base = os.path.basename(path)
            if base == 'box64':
                i = bisect.bisect_right(baddrs, ip) - 1
                fn = bnames[i] if i >= 0 else 'box64 ?'
                b = box64_group(fn)
            elif path and not path.startswith('['):
                b = 'native: ' + base
                fn = base
            else:
                i = bisect.bisect_right(pkeys, ip) - 1
                if i >= 0 and pkeys[i] <= ip < pkeys[i] + max(pvals[i][0], 4):
                    mod, sym = x86_module(pvals[i][1])
                    label = {'NITW.x86_64': 'x86: Unity player (engine)', 'libmono.so': 'x86: libmono (Mono runtime)',
                             'JIT / anonymous x86 code': 'x86: Mono JIT code (game C#)'}.get(mod)
                    if not label:
                        label = 'x86: FMOD' if 'fmod' in mod.lower() else f'x86: {mod}'
                    b = label
                    desc = pvals[i][1]
                    if sym:
                        fn = f'{mod}/{sym.split(" +")[0]}'
                    elif mod == 'JIT / anonymous x86 code':        # group JIT'd code by 1 KB range
                        fn = f'JIT code @ 0x{int(desc, 16) & ~0x3ff:x}'
                    else:                                         # unnamed code: group by 1 KB range
                        m = re.search(r'\+ 0x([0-9a-f]+)', desc)
                        fn = f'{mod} +0x{int(m.group(1), 16) & ~0x3ff:x}' if m else desc
                else:
                    unmapped_dyn += c
                    b, fn = ('dynarec code (no map entry)' if pkeys else 'dynarec / anonymous code'), '?'
        bucket[tid][b] += c
        funcs[tid][fn] += c

    all_total = sum(totals.values()) or 1
    print(f'profile: {d}\n{all_total} samples (~{all_total / 1000:.0f} CPU-seconds at 1 kHz), '
          f'{len(pkeys)} perfmap entries, {len(baddrs)} box64 symbols\n')
    ordered = sorted(totals, key=lambda t: -totals[t])
    print('threads (share of all samples):')
    for tid in ordered[:12]:
        tag = ' <- main' if tid == min(totals) else (' <- render' if tid == render_tid else '')
        print(f'  {tid:>7} {threads.get(tid, "?"):<18} {totals[tid]:>7}  {100 * totals[tid] / all_total:5.1f}%{tag}')
    main_tid = min(totals)            # the main thread has the lowest tid (tid == pid)
    show = [main_tid] + ([render_tid] if render_tid in totals else []) + \
           [t for t in ordered if t not in (main_tid, render_tid)][:2]
    for tid in show:
        n = totals[tid] or 1
        role = 'main thread' if tid == main_tid else ('render thread' if tid == render_tid else 'thread')
        print(f'\n=== {role} {tid} ({threads.get(tid, "?")}), {totals[tid]} samples ===')
        for b, c in bucket[tid].most_common():
            print(f'  {100 * c / n:5.1f}%  {b}')
        print(f'  -- top {a.top} functions --')
        for fn, c in funcs[tid].most_common(a.top):
            print(f'  {100 * c / n:5.1f}%  {fn}')
    all_b = collections.Counter()
    for tid in bucket:
        all_b.update(bucket[tid])
    print('\n=== all threads ===')
    for b, c in all_b.most_common():
        print(f'  {100 * c / all_total:5.1f}%  {b}')
    if unmapped_dyn:
        print(f'\n{unmapped_dyn} samples in anonymous code without a perfmap entry')


if __name__ == '__main__':
    main()
