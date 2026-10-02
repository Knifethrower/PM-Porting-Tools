"""Patch a Godot 4 pack (format 2, Godot 4.0-4.3) in place: replace existing files by appending new data
at the end of the pack and repointing their directory entries. The directory size never
changes, so only existing paths can be replaced.

usage: pck_patch.py <pck> <res://path>=<local file> [...]
       pck_patch.py <pck> --classes <stub dir> <abs dir on target>
           merges every `class_name` script in <stub dir> into
           res://.godot/global_script_class_cache.cfg, pointing at <abs dir on target>/<file>
       pck_patch.py <pck> --list
           prints every file in the pack with its size
Work on a copy of the pack. From Dome Keeper.
"""
import hashlib, os, re, struct, sys

HEADER = 4 + 16 + 12 + 64


def read_dir(f):
    f.seek(0)
    h = f.read(HEADER)
    assert h[:4] == b'GDPC' and struct.unpack_from('<I', h, 4)[0] == 2
    base = struct.unpack_from('<Q', h, 24)[0]
    count = struct.unpack('<I', f.read(4))[0]
    entries = {}
    for _ in range(count):
        n = struct.unpack('<I', f.read(4))[0]
        name = f.read(n).rstrip(b'\0').decode()
        pos = f.tell()  # offset u64, size u64, md5[16], flags u32
        off, size = struct.unpack('<QQ', f.read(16))
        f.read(20)
        entries[name] = (pos, off, size)
    return base, entries


def replace(f, base, entries, path, data):
    pos, _, _ = entries[path]
    f.seek(0, 2)
    end = f.tell()
    pad = (-end) % 16
    f.write(b'\0' * pad)
    new_off = end + pad - base
    f.write(data)
    f.seek(pos)
    f.write(struct.pack('<QQ', new_off, len(data)) + hashlib.md5(data).digest())


def merged_class_cache(f, base, entries, stub_dir, target_dir):
    _, off, size = entries['res://.godot/global_script_class_cache.cfg']
    f.seek(base + off)
    text = f.read(size).decode()
    items = []
    for fn in sorted(os.listdir(stub_dir)):
        if not fn.endswith('.gd'):
            continue
        src = open(os.path.join(stub_dir, fn), encoding='utf-8').read()
        cm = re.search(r'^class_name\s+(\w+)', src, re.M)
        if not cm:
            continue
        bm = re.search(r'^extends\s+(\w+)', src, re.M)
        assert f'&"{cm.group(1)}"' not in text, cm.group(1) + ' already in class cache'
        items.append('{\n"base": &"%s",\n"class": &"%s",\n"icon": "",\n"language": &"GDScript",\n"path": "%s"\n}'
                     % (bm.group(1) if bm else 'RefCounted', cm.group(1), target_dir.rstrip('/') + '/' + fn))
    assert text.rstrip().endswith('}])'), 'unexpected class cache layout'
    head = text.rstrip()[:-2]
    return (head + ', ' + ', '.join(items) + '])\n').encode()


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    pck = sys.argv[1]
    with open(pck, 'rb' if sys.argv[2] == '--list' else 'r+b') as f:
        base, entries = read_dir(f)
        if sys.argv[2] == '--list':
            for path in sorted(entries):
                print(f'{entries[path][2]:>10}  {path}')
        elif sys.argv[2] == '--classes':
            data = merged_class_cache(f, base, entries, sys.argv[3], sys.argv[4])
            replace(f, base, entries, 'res://.godot/global_script_class_cache.cfg', data)
            print('class cache merged:', len(data), 'bytes')
        else:
            for arg in sys.argv[2:]:
                path, local = arg.split('=', 1)
                replace(f, base, entries, path, open(local, 'rb').read())
                print('replaced', path)
