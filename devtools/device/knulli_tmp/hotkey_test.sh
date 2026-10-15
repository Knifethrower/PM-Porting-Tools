# hotkey_test.sh "<Port>.sh" <binary>: start the port (virtual pad), press Select+Start, report
# whether the game process is gone and the launcher finished (front end restarted afterwards).
# Needs pad_start.sh running. License: 0BSD.
pidsof() { for p in /proc/[0-9]*; do [ "$(cat $p/comm 2>/dev/null)" = "${1:0:15}" ] && echo ${p#/proc/}; done; }
bash /tmp/run_port.sh "$1" >/dev/null; sleep 12
[ -n "$(pidsof $2)" ] && echo "running" || echo "NOT running before hotkey"
timeout 5 sh -c "echo select+start:1:1:0.6 > /tmp/pad.fifo"; sleep 5
if [ -n "$(pidsof $2)" ]; then echo "$1: STILL RUNNING after Select+Start"; kill -9 $(pidsof $2); else echo "$1: exited on Select+Start"; fi
sleep 1; pgrep -f port_run.sh >/dev/null && echo "launcher still running" || echo "launcher finished"
/etc/init.d/S31emulationstation start >/dev/null 2>&1 &
