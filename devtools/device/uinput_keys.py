#!/usr/bin/env python3
"""Press keys on a virtual uinput keyboard ON the handheld (like gptokeyb's own virtual device),
so a test run can get from the title screen into the game without anyone holding the pad.
Runs with the device's own python3 (no packages); needs /dev/uinput (root).

usage: uinput_keys.py <spec> ...
  spec = <keys>:<count>:<interval>[:<hold>]
    keys      key names joined with + to hold them together (up+left walks diagonally)
    count     presses; interval = seconds after each press; hold = seconds down (default 0.08)
  wait:1:<s>  pause for <s> seconds
  e.g. uinput_keys.py enter:1:2 z:3:0.5 right:1:0:1.5 wait:1:4 esc:1:1
key names: a-z, 0-9, enter, esc, space, tab, backspace, up, down, left, right, lshift, rshift,
  lctrl, rctrl, lalt, ralt, f1-f12, pageup, pagedown, home, end, minus, equal, comma, dot, slash
UINPUT_SETTLE (default 2): seconds to wait after creating the device so Weston/Xwayland/SDL pick it up.
UINPUT_FIFO=<path>: after the specs, keep the device and run spec lines written to that FIFO
  (created if missing) until a line "quit": create the keyboard before the game starts (SDL on
  KMSDRM may not see a keyboard plugged in later), then step through it with screenshots:
  UINPUT_FIFO=/tmp/keys.fifo uinput_keys.py wait:1:0 &  ...  echo "z:1:1 down:2:0.3" > /tmp/keys.fifo
From Ittle Dew (pctest/cube_uinput.py). License: 0BSD.
"""
import fcntl, os, struct, sys, time

KEYS = {'esc': 1, 'minus': 12, 'equal': 13, 'backspace': 14, 'tab': 15, 'enter': 28, 'lctrl': 29,
        'lshift': 42, 'comma': 51, 'dot': 52, 'slash': 53, 'rshift': 54, 'lalt': 56, 'space': 57,
        'rctrl': 97, 'ralt': 100, 'home': 102, 'up': 103, 'pageup': 104, 'left': 105, 'right': 106,
        'end': 107, 'down': 108, 'pagedown': 109}
for row, start in (('qwertyuiop', 16), ('asdfghjkl', 30), ('zxcvbnm', 44)):
    for i, ch in enumerate(row):
        KEYS[ch] = start + i
for i, ch in enumerate('1234567890'):
    KEYS[ch] = 2 + i
for i in range(10):
    KEYS[f'f{i + 1}'] = 59 + i
KEYS['f11'], KEYS['f12'] = 87, 88

UI_SET_EVBIT, UI_SET_KEYBIT, UI_DEV_CREATE, UI_DEV_DESTROY = 0x40045564, 0x40045565, 0x5501, 0x5502
EV_SYN, EV_KEY = 0, 1

if len(sys.argv) < 2:
    sys.exit(__doc__)


def parse(args):                               # parse everything first: no half-run on a typo
    specs = []
    for arg in args:
        parts = arg.split(':')
        if len(parts) < 3:
            raise ValueError(f'bad spec {arg!r}')
        keys = parts[0]
        if keys != 'wait':
            unknown = [k for k in keys.split('+') if k not in KEYS]
            if unknown:
                raise ValueError(f'unknown key(s) {unknown}')
        specs.append((keys, int(parts[1]), float(parts[2]), float(parts[3]) if len(parts) > 3 else 0.08))
    return specs


try:
    specs = parse(sys.argv[1:])
except ValueError as e:
    sys.exit(str(e))

fd = os.open('/dev/uinput', os.O_WRONLY | os.O_NONBLOCK)
fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
for k in set(KEYS.values()):
    fcntl.ioctl(fd, UI_SET_KEYBIT, k)
# struct uinput_user_dev: name[80], struct input_id {u16 bustype, vendor, product, version}, u32 ff_effects_max,
# int absmax[64], absmin[64], absfuzz[64], absflat[64]
os.write(fd, struct.pack('80sHHHHI', b'uinput-test-keyboard', 3, 0x1234, 0x5678, 1, 0) + b'\0' * (4 * 64 * 4))
fcntl.ioctl(fd, UI_DEV_CREATE)
time.sleep(float(os.environ.get('UINPUT_SETTLE', '2')))


def emit(t, c, v):
    os.write(fd, struct.pack('llHHi', 0, 0, t, c, v))


def run(specs):
    for keys, count, interval, hold in specs:
        if keys == 'wait':
            time.sleep(interval)
            continue
        codes = [KEYS[k] for k in keys.split('+')]
        for _ in range(count):
            for c in codes:
                emit(EV_KEY, c, 1)
            emit(EV_SYN, 0, 0)
            time.sleep(hold)
            for c in codes:
                emit(EV_KEY, c, 0)
            emit(EV_SYN, 0, 0)
            time.sleep(interval)


try:
    run(specs)
    fifo = os.environ.get('UINPUT_FIFO')
    if fifo:
        if not os.path.exists(fifo):
            os.mkfifo(fifo)
        done = False
        while not done:
            with open(fifo) as f:
                for line in f:
                    if line.strip() == 'quit':
                        done = True
                        break
                    try:
                        run(parse(line.split()))
                    except ValueError as e:
                        print(e, flush=True)
    time.sleep(0.3)
finally:
    fcntl.ioctl(fd, UI_DEV_DESTROY)
    os.close(fd)
