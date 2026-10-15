# pad_start.sh: (re)start the FIFO virtual pad (uinput_pad.py in /tmp) and write /tmp/vpad.env
# (its SDL mapping + ignore the real pad) and /tmp/test.sed (launchers source vpad.env).
# Run it as its own command: its kill loop matches any cmdline containing uinput_pad.py. 0BSD.
for p in /proc/[0-9]*; do grep -q uinput_pad.py $p/cmdline 2>/dev/null && [ ${p#/proc/} != $$ ] && kill ${p#/proc/}; done
rm -f /tmp/pad.fifo
UINPUT_FIFO=/tmp/pad.fifo setsid nohup python3 /tmp/uinput_pad.py wait:1:0 > /tmp/pad.txt 2>&1 < /dev/null &
sleep 3; grep -c "PM Virtual Pad" /proc/bus/input/devices
python3 /tmp/uinput_pad.py --env > /tmp/vpad.env
printf '%s\n' 's|^export SDL_GAMECONTROLLERCONFIG=.*|&\nsource /tmp/vpad.env|' > /tmp/test.sed
