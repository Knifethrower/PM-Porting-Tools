# keys_start.sh: (re)start the FIFO virtual keyboard (uinput_keys.py in /tmp) on /tmp/keys.fifo,
# for games driven by keys before a gptokeyb2 launcher exists. Start it before the game.
# Run as its own command (kill loop matches cmdlines with uinput_keys.py). 0BSD.
for p in /proc/[0-9]*; do grep -q uinput_keys.py $p/cmdline 2>/dev/null && [ ${p#/proc/} != $$ ] && kill ${p#/proc/}; done
rm -f /tmp/keys.fifo
UINPUT_FIFO=/tmp/keys.fifo setsid nohup python3 /tmp/uinput_keys.py wait:1:0 > /tmp/keys.txt 2>&1 < /dev/null &
sleep 3; echo keys ready
