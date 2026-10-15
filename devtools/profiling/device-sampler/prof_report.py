"""Report for the device test builds' sampling profiler (prof.c).
usage: prof_report.py <dir with prof.bin, prof_maps.txt, prof_info.txt> <unstripped binary> [top N]
Prints the share of CPU samples per thread, per mapped file and per function of the game
binary (main thread and all threads). Run in WSL (uses readelf and c++filt)."""
import collections, os, struct, subprocess, sys

d, binary = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40

raw = open(os.path.join(d, 'prof.bin'), 'rb').read()
samples = [struct.unpack_from('<Q', raw, i)[0] for i in range(0, len(raw) - 7, 8)]
main_tid = None
for line in open(os.path.join(d, 'prof_info.txt')):
    if line.startswith('main_tid'):
        main_tid = int(line.split()[1])

maps = []   # (start, end, offset, name)
for line in open(os.path.join(d, 'prof_maps.txt')):
    f = line.split()
    if len(f) < 5:
        continue
    a, b = [int(x, 16) for x in f[0].split('-')]
    maps.append((a, b, int(f[2], 16), f[5] if len(f) > 5 else '[anon]'))

bin_name = None
for a, b, off, name in maps:
    if 'torustrooper' in name or 'Torus_Trooper' in name:
        bin_name = name
        break
base = min(a - off for a, b, off, name in maps if name == bin_name) if bin_name else 0

syms = []   # (addr, size, name)
out = subprocess.run(['readelf', '-sW', binary], capture_output=True, text=True).stdout
for line in out.splitlines():
    f = line.split()
    if len(f) >= 8 and f[3] == 'FUNC' and f[6] != 'UND':
        try:
            syms.append((int(f[1], 16), int(f[2]), f[7]))
        except ValueError:
            pass
syms.sort()
names = [s[2] for s in syms]
dem = subprocess.run(['c++filt', '-s', 'dlang'], input='\n'.join(names), capture_output=True, text=True).stdout.splitlines()
if len(dem) == len(names):
    syms = [(s[0], s[1], n) for s, n in zip(syms, dem)]
addrs = [s[0] for s in syms]

import bisect


def where(pc):
    for a, b, off, name in maps:
        if a <= pc < b:
            if name == bin_name:
                rel = pc - base
                i = bisect.bisect_right(addrs, rel) - 1
                if i >= 0 and (syms[i][1] == 0 or rel < syms[i][0] + syms[i][1] + 64):
                    return 'game', syms[i][2]
                return 'game', hex(rel)
            return os.path.basename(name), None
    return '[unmapped]', None


total = len(samples)
by_thread = collections.Counter()
by_file = collections.Counter()
by_file_main = collections.Counter()
fn_main = collections.Counter()
fn_other = collections.Counter()
for v in samples:
    tid, pc = v >> 48, v & 0xFFFFFFFFFFFF
    is_main = tid == main_tid
    by_thread['main' if is_main else 'thread %d' % tid] += 1
    f, fn = where(pc)
    by_file[f] += 1
    if is_main:
        by_file_main[f] += 1
    if fn:
        (fn_main if is_main else fn_other)[fn] += 1

print('%d samples (2 ms of CPU time each = %.1f s of CPU)' % (total, total * 0.002))
print('\n== per thread')
for k, n in by_thread.most_common(12):
    print('%6.1f%%  %s' % (100.0 * n / total, k))
print('\n== per file, all threads (main thread only)')
for k, n in by_file.most_common(15):
    print('%6.1f%%  (%5.1f%%)  %s' % (100.0 * n / total, 100.0 * by_file_main[k] / total, k))
print('\n== game functions, main thread, %% of all samples')
for k, n in fn_main.most_common(top):
    print('%6.1f%%  %s' % (100.0 * n / total, k[:110]))
if fn_other:
    print('\n== game functions, other threads')
    for k, n in fn_other.most_common(8):
        print('%6.1f%%  %s' % (100.0 * n / total, k[:110]))
