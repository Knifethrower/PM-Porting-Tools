#!/usr/bin/env python3
"""Virtual gamepad ON the handheld (uinput), so a test run can check a game's own controller
support (SDL GameController) without anyone holding the pad. Runs with the device's own python3
(no packages); needs /dev/uinput (root). Same spec syntax as uinput_keys.py.

usage: uinput_pad.py <spec> ...      spec = <buttons>:<count>:<interval>[:<hold>]
  buttons  names joined with + to hold them together: a b x y l1 r1 l2 r2 select start guide l3 r3
           up down left right (D-pad), lleft lright lup ldown rleft rright rup rdown (stick fully over)
  wait:1:<s>  pause for <s> seconds
  e.g. uinput_pad.py wait:1:3 a:1:2 down:2:0.5 lright:1:0:2 start:1:1
UINPUT_FIFO=<path>: after the specs, keep the pad and run spec lines written to that FIFO until
  a line "quit" (create the pad before the game starts, then step through it with screenshots).
UINPUT_SETTLE (default 2): seconds to wait after creating the device.

The pad is USB 1234:5678 "PM Virtual Pad" with Xbox-style evdev codes. The game needs its SDL
mapping and should ignore the real pad; in the launcher (after its SDL_GAMECONTROLLERCONFIG line):
  export SDL_GAMECONTROLLERCONFIG="$(python3 uinput_pad.py --mapping)
$SDL_GAMECONTROLLERCONFIG"
  export SDL_GAMECONTROLLER_IGNORE_DEVICES=0x<vid>/0x<pid>      # the real pad, see --ignore
  (uinput_pad.py --env prints both lines for the pad found in /proc/bus/input/devices.)
License: 0BSD.
"""
import fcntl, os, re, struct, sys, time

# evdev codes, in SDL's button order (SDL numbers buttons by ascending key code from BTN_JOYSTICK)
BUTTONS = [('a', 0x130), ('b', 0x131), ('y', 0x133), ('x', 0x134), ('l1', 0x136), ('r1', 0x137),
           ('l2', 0x138), ('r2', 0x139), ('select', 0x13a), ('start', 0x13b), ('guide', 0x13c),
           ('l3', 0x13d), ('r3', 0x13e), ('up', 0x220), ('down', 0x221), ('left', 0x222), ('right', 0x223)]
SDL_NAMES = {'a': 'a', 'b': 'b', 'y': 'y', 'x': 'x', 'l1': 'leftshoulder', 'r1': 'rightshoulder',
             'l2': 'lefttrigger', 'r2': 'righttrigger', 'select': 'back', 'start': 'start', 'guide': 'guide',
             'l3': 'leftstick', 'r3': 'rightstick', 'up': 'dpup', 'down': 'dpdown', 'left': 'dpleft', 'right': 'dpright'}
AXES = [('lx', 0x00, 'leftx'), ('ly', 0x01, 'lefty'), ('rx', 0x03, 'rightx'), ('ry', 0x04, 'righty')]
KEYS = dict(BUTTONS)
for n, code, _ in AXES:
    lo, hi = ('left', 'right') if n[1] == 'x' else ('up', 'down')
    KEYS[n[0] + lo] = ('abs', code, -32767)
    KEYS[n[0] + hi] = ('abs', code, 32767)
VID, PID, VER = 0x1234, 0x5678, 1
GUID = '03000000%02x%02x0000%02x%02x0000%02x%02x0000' % (VID & 255, VID >> 8, PID & 255, PID >> 8, VER & 255, VER >> 8)


def mapping():
    m = ','.join(f'{SDL_NAMES[n]}:b{i}' for i, (n, _) in enumerate(BUTTONS))
    m += ',' + ','.join(f'{s}:a{i}' for i, (_, _, s) in enumerate(AXES))
    return f'{GUID},PM Virtual Pad,{m},platform:Linux,'


def real_pads():
    """vendor/product of the joysticks in /proc/bus/input/devices (not this pad)"""
    out = []
    for block in open('/proc/bus/input/devices').read().split('\n\n'):
        i = re.search(r'Vendor=(\w+) Product=(\w+)', block)
        if i and re.search(r'H: Handlers=.*\bjs\d', block) and 'PM Virtual Pad' not in block:
            out.append(f'0x{i.group(1)}/0x{i.group(2)}')
    return out


if len(sys.argv) < 2:
    sys.exit(__doc__)
if sys.argv[1] == '--mapping':
    print(mapping())
    sys.exit()
if sys.argv[1] == '--env':
    print(f'export SDL_GAMECONTROLLERCONFIG="{mapping()}\n$SDL_GAMECONTROLLERCONFIG"')
    print(f'export SDL_GAMECONTROLLER_IGNORE_DEVICES={",".join(real_pads())}')
    sys.exit()


def parse(args):                               # parse everything first: no half-run on a typo
    specs = []
    for arg in args:
        parts = arg.split(':')
        if len(parts) < 3:
            raise ValueError(f'bad spec {arg!r}')
        if parts[0] != 'wait':
            unknown = [k for k in parts[0].split('+') if k not in KEYS]
            if unknown:
                raise ValueError(f'unknown button(s) {unknown}')
        specs.append((parts[0], int(parts[1]), float(parts[2]), float(parts[3]) if len(parts) > 3 else 0.1))
    return specs


try:
    specs = parse(sys.argv[1:])
except ValueError as e:
    sys.exit(str(e))

UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_ABSBIT, UI_DEV_CREATE, UI_DEV_DESTROY = 0x40045564, 0x40045565, 0x40045567, 0x5501, 0x5502
EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
fd = os.open('/dev/uinput', os.O_WRONLY | os.O_NONBLOCK)
fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
fcntl.ioctl(fd, UI_SET_EVBIT, EV_ABS)
for _, code in BUTTONS:
    fcntl.ioctl(fd, UI_SET_KEYBIT, code)
absmax, absmin = [0] * 64, [0] * 64
for _, code, _ in AXES:
    fcntl.ioctl(fd, UI_SET_ABSBIT, code)
    absmax[code], absmin[code] = 32767, -32768
# struct uinput_user_dev: name[80], input_id {bustype, vendor, product, version}, ff_effects_max,
# absmax[64], absmin[64], absfuzz[64], absflat[64]
os.write(fd, struct.pack('80sHHHHI', b'PM Virtual Pad', 3, VID, PID, VER, 0)
         + struct.pack('64i', *absmax) + struct.pack('64i', *absmin) + b'\0' * (2 * 64 * 4))
fcntl.ioctl(fd, UI_DEV_CREATE)
time.sleep(float(os.environ.get('UINPUT_SETTLE', '2')))


def emit(t, c, v):
    os.write(fd, struct.pack('llHHi', 0, 0, t, c, v))


def press(names, down):
    for n in names:
        k = KEYS[n]
        if isinstance(k, tuple):
            emit(EV_ABS, k[1], k[2] if down else 0)
        else:
            emit(EV_KEY, k, 1 if down else 0)
    emit(EV_SYN, 0, 0)


def run(specs):
    for names, count, interval, hold in specs:
        if names == 'wait':
            time.sleep(interval)
            continue
        for _ in range(count):
            press(names.split('+'), True)
            time.sleep(hold)
            press(names.split('+'), False)
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
