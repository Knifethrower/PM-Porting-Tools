"""Patch Usagi Yojimbo's Assembly-UnityScript.dll for the handheld port (pure Python, runs on the device).

Changes (only the port's own bytes; the game's code is taken from your copy of the file):
  * MCP.SetupCInput: the 13 raw-gamepad alternates of the default cInput bindings
    ("Joystick1Button0", "Joy1 Axis 3-", ...) -> "None". The handheld's buttons reach the game as
    keyboard keys through gptokeyb; Unity would also read the pad itself and fire a second,
    different action.
  * Level9Controller.Update and MansionRoom.Update: Space, Enter or joystick button 0 also take
    the two mouse-only level exits (click on the waterfall arrow / hover over the Mansion Boss
    exit arrow). Each method gets a new body = a short key check + the method's original code,
    copied unchanged (its branches are relative, so they stay valid), placed in a new PE section.

Locked to the Steam release (Assembly-UnityScript.dll, SHA-1 below): offsets and metadata tokens
are that file's. Usage: python scriptpatch.py <in dll> <out dll>
"""
import hashlib, struct, sys

ORIGINAL_SHA1 = '09b4f0928bcbccd518273e709ddb9f50b5b2bbc3'

# metadata tokens in the Steam Assembly-UnityScript.dll
TOK = dict(
    getKeyDown=0x0a0000c2,            # UnityEngine.Input::GetKeyDown(KeyCode)
    startCoroutine=0x0a00005d,        # MonoBehaviour::StartCoroutine_Auto(IEnumerator)
    l9_checkInput=0x040007d1, l9_fade=0x0600041d, us_WaterfallArrow=0x700071db,
    mr_checkInput=0x0400032b, mr_mcp=0x0400032d, setLevel=0x0600016b,
    get_loadedLevelName=0x0a000002, contains=0x0a000025, loadLevel=0x0a0000af,
    us_MansionBoss=0x70001155, us_Loading=0x70001b22, us_None=0x7000733d,
)
LEVEL9 = dict(row=0x4e086, body=0x44474)       # MethodDef row (RVA field) and fat body header
MANSION = dict(row=0x4be3c, body=0x25238)
MCP_TOKENS = [0x23ede, 0x23ef3, 0x23f08, 0x23f1d, 0x23f32, 0x23f47, 0x23f5c, 0x23f71,
              0x23f86, 0x23f9b, 0x23fb0, 0x23fc5, 0x23fda]   # ldstr operands in SetupCInput
KEYS = (32, 13, 330)                          # KeyCode.Space, Return, JoystickButton0


class IL:
    """Tiny IL assembler with long-form branches to labels."""
    def __init__(self):
        self.b, self.fix, self.lab = bytearray(), [], {}

    def op(self, *bs):
        self.b += bytes(bs); return self

    def tok(self, opcode, token):
        self.b += bytes([opcode]) + struct.pack('<I', token); return self

    def i4(self, v):
        self.b += b'\x20' + struct.pack('<i', v); return self

    def br(self, opcode, label):            # 0x38 br, 0x39 brfalse, 0x3a brtrue
        self.b.append(opcode); self.fix.append((len(self.b), label)); self.b += b'\0\0\0\0'; return self

    def label(self, name):
        self.lab[name] = len(self.b); return self

    def build(self, orig_label_pos):
        self.lab['ORIG'] = orig_label_pos
        for pos, name in self.fix:
            struct.pack_into('<i', self.b, pos, self.lab[name] - (pos + 4))
        return bytes(self.b)


def key_check(il):
    for k in KEYS:
        il.i4(k).tok(0x28, TOK['getKeyDown']).br(0x3a, 'GO')       # if GetKeyDown(k) goto GO
    il.br(0x38, 'ORIG')


def level9_prefix():
    il = IL()
    il.op(0x02).tok(0x7b, TOK['l9_checkInput']).br(0x39, 'ORIG')   # if (!checkInput) -> original code
    key_check(il)
    il.label('GO').op(0x02, 0x16).tok(0x7d, TOK['l9_checkInput'])   # checkInput = false
    il.op(0x02, 0x02).tok(0x72, TOK['us_WaterfallArrow']).tok(0x6f, TOK['l9_fade'])
    il.tok(0x6f, TOK['startCoroutine']).op(0x26, 0x2a)               # StartCoroutine_Auto(...); pop; ret
    return il


def mansion_prefix():
    il = IL()
    il.op(0x02).tok(0x7b, TOK['mr_checkInput']).br(0x39, 'ORIG')
    il.tok(0x28, TOK['get_loadedLevelName']).tok(0x72, TOK['us_MansionBoss'])
    il.tok(0x6f, TOK['contains']).br(0x39, 'ORIG')                   # only in the Mansion Boss level
    key_check(il)
    il.label('GO').op(0x02).tok(0x7b, TOK['mr_mcp']).op(0x19).tok(0x6f, TOK['setLevel'])   # _MCP.SetLevelToLoadByIndex(3)
    il.tok(0x72, TOK['us_Loading']).tok(0x28, TOK['loadLevel']).op(0x2a)                 # LoadLevel("Loading"); ret
    return il


def new_body(d, body_off, il):
    flags, maxstack, size, localsig = struct.unpack_from('<HHII', d, body_off)
    assert flags & 3 == 3 and not flags & 0x8, 'unexpected method header'
    orig = d[body_off + 12:body_off + 12 + size]
    code = il.build(orig_label_pos=len(il.b)) + orig      # ORIG = first byte after the prefix
    return struct.pack('<HHII', flags, max(maxstack, 3), len(code), localsig) + code


def align(v, a):
    return (v + a - 1) & ~(a - 1)


def patch(data):
    d = bytearray(data)
    for off in MCP_TOKENS:                                            # "Joystick..."/"Joy1 Axis" -> "None"
        assert d[off - 1] == 0x72 and d[off + 3] == 0x70
        struct.pack_into('<I', d, off, TOK['us_None'])
    bodies = [new_body(d, LEVEL9['body'], level9_prefix()), new_body(d, MANSION['body'], mansion_prefix())]
    # new section at the end of the image holding both bodies (4-byte aligned)
    e, = struct.unpack_from('<I', d, 0x3c)
    nsec, = struct.unpack_from('<H', d, e + 6)
    optsize, = struct.unpack_from('<H', d, e + 20)
    opt = e + 24
    salign, falign = struct.unpack_from('<II', d, opt + 32)
    size_headers, = struct.unpack_from('<I', d, opt + 60)
    sect = opt + optsize
    new_hdr = sect + 40 * nsec
    assert new_hdr + 40 <= size_headers, 'no room for another section header'
    last = struct.unpack_from('<8sIIII', d, sect + 40 * (nsec - 1))
    va = align(last[2] + last[1], salign)
    raw = bytearray()
    rvas = []
    for b in bodies:
        raw += b'\0' * ((-len(raw)) % 4)
        rvas.append(va + len(raw)); raw += b
    vsize = len(raw)
    raw += b'\0' * ((-len(raw)) % falign)
    ptr = align(len(d), falign)
    d += b'\0' * (ptr - len(d))
    struct.pack_into('<8sIIIIIIHHI', d, new_hdr, b'.usagi', vsize, va, len(raw), ptr, 0, 0, 0, 0, 0x60000020)
    struct.pack_into('<H', d, e + 6, nsec + 1)
    struct.pack_into('<I', d, opt + 56, align(va + vsize, salign))      # SizeOfImage
    d += raw
    for m, rva in zip((LEVEL9, MANSION), rvas):                           # MethodDef.RVA -> new bodies
        struct.pack_into('<I', d, m['row'], rva)
    return bytes(d)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, 'rb').read()
    sha1 = hashlib.sha1(data).hexdigest()
    if sha1 != ORIGINAL_SHA1:
        sys.exit(f'{src}: unexpected version (SHA-1 {sha1}); this patch is for the Steam release')
    out = patch(data)
    with open(dst, 'wb') as f:
        f.write(out)
    print(f'scripts patched: {len(MCP_TOKENS)} gamepad bindings removed, 2 level exits accept keys')


if __name__ == '__main__':
    main()
