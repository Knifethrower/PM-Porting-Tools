#!/usr/bin/env python3
"""Device side: where a process's memory goes. Sums Rss per mapping name from /proc/<pid>/smaps
and prints the total and the 15 biggest (game data files, [heap], [anon], libraries).
usage: smaps_summary.py <pid>      (the device's own python3; root for other users' processes)
From Ittle Dew (pctest/smaps_summary.py)."""
import collections, re, sys

if len(sys.argv) != 2:
    sys.exit(__doc__)
agg = collections.Counter()
cur = None
for line in open(f'/proc/{sys.argv[1]}/smaps'):
    if re.match(r'^[0-9a-f]+-[0-9a-f]+ ', line):
        parts = line.split()
        cur = re.sub(r'.*/', '', parts[5]) if len(parts) > 5 else '[anon]'
    elif line.startswith('Rss:'):
        agg[cur] += int(line.split()[1])
print('total Rss %d MB' % (sum(agg.values()) // 1024))
for name, kb in agg.most_common(15):
    print('  %6d MB  %s' % (kb // 1024, name))
