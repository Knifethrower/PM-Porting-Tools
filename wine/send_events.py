# Writes raw x86_64 struct input_event records to stdout: pointer moves, 2 left clicks, 1 right
# click, key A, like gptokeyb's virtual device would produce (for xbridge_input_test.sh).
import struct, sys, time

EV_SYN, EV_KEY, EV_REL = 0, 1, 2
REL_X, REL_Y = 0, 1
BTN_LEFT, BTN_RIGHT, KEY_A = 0x110, 0x111, 30
out = sys.stdout.buffer

def ev(t, c, v):
    out.write(struct.pack("llHHi", 0, 0, t, c, v))

def syn():
    ev(EV_SYN, 0, 0)
    out.flush()
    time.sleep(0.05)

for _ in range(20):          # move up-left
    ev(EV_REL, REL_X, -8); ev(EV_REL, REL_Y, -5); syn()
for b in (BTN_LEFT, BTN_LEFT, BTN_RIGHT):
    ev(EV_KEY, b, 1); syn()
    ev(EV_KEY, b, 0); syn()
ev(EV_KEY, KEY_A, 1); syn()
ev(EV_KEY, KEY_A, 0); syn()
