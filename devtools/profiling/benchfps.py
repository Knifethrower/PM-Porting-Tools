"""Median FPS of the last N seconds before quitting (skipping the final few seconds of exit).
usage: python benchfps.py fps.txt [seconds=60] [skip_end=3]"""
import re, statistics, sys
v = [float(m.group(1)) for m in re.finditer(r'FPS: ([0-9.]+)', open(sys.argv[1]).read())]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
skip = int(sys.argv[3]) if len(sys.argv) > 3 else 3
w = v[-(n + skip):-skip] if skip else v[-n:]
print(f'{len(v)} s logged; window {len(w)} s: median {statistics.median(w):.1f}, mean {statistics.mean(w):.1f}, '
      f'min {min(w):.1f}, max {max(w):.1f}, stdev {statistics.pstdev(w):.2f}')
print('last 75 s:', ' '.join(f'{x:.0f}' for x in v[-75:]))
