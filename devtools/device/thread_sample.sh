#!/bin/bash
# Device side: which threads of a running game use the CPU, and where the others wait.
#   dev.sh "bash -s <process name> [rounds] [seconds]" < thread_sample.sh
# Per round (default 3 x 5 s): major page faults and MemAvailable over the interval, then the six
# busiest threads: CPU ticks used, tid, thread name, state and kernel wait channel (wchan).
# Finds hangs (every thread in futex_wait / pipe_read), page-fault storms on 1 GB devices and
# which emulated thread is busy under box64. Process name = /proc/<pid>/comm (15 characters).
# From Ittle Dew (pctest/brick_sample.sh).
NAME=$1; ROUNDS=${2:-3}; SECS=${3:-5}
for d in /proc/[0-9]*; do [ "$(cat $d/comm 2>/dev/null)" = "$NAME" ] && P=${d#/proc/}; done
[ -n "$P" ] || { echo "no process named $NAME"; exit 1; }
echo "pid $P, running for $(ps -o pid,etime | awk -v p=$P '$1==p{print $2}')"
for r in $(seq "$ROUNDS"); do
  for t in /proc/$P/task/*; do echo "${t##*/} $(cut -d' ' -f14,15 $t/stat 2>/dev/null)"; done > /tmp/ts_a
  f0=$(awk '/pgmajfault/{print $2}' /proc/vmstat); sleep "$SECS"; f1=$(awk '/pgmajfault/{print $2}' /proc/vmstat)
  echo "--- round $r: majfaults +$((f1 - f0)), MemAvailable $(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo) MB"
  for t in /proc/$P/task/*; do
    id=${t##*/}; set -- $(cut -d' ' -f14,15 $t/stat 2>/dev/null) 0 0
    old=$(grep "^$id " /tmp/ts_a | cut -d' ' -f2,3); set -- $1 $2 $old 0 0
    echo "$(( $1 + $2 - $3 - $4 )) $id $(cat $t/comm) $(cut -d' ' -f3 $t/stat 2>/dev/null) $(cat $t/wchan 2>/dev/null)"
  done | sort -rn | head -6
done
rm -f /tmp/ts_a
