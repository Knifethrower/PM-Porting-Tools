"""Make every imported Ogg under a pack folder (default res://content/music/) load lazily.

For each music track, writes <out_dir>/<name>.tres (a LazyAudioStream pointing at the
original imported .oggvorbisstr inside the pack) and repoints the track's .import remap
in the pack to it. <target_dir> is where out_dir will live on the device (absolute).

usage: lazy_music.py <pck> <out_dir> <target_dir> <abs path of LazyAudioStream.gd on target> [res:// prefix]
Godot 4.x games that preload their whole soundtrack keep every decoded-ready Ogg in RAM; with this
only the playing track is loaded. From Dome Keeper (Godot 4.3). Patches the pck in place: work on a copy.
"""
import os, re, struct, sys
import pck_patch as P


def main(pck, out_dir, target_dir, script_path, prefix='res://content/music/'):
    os.makedirs(out_dir, exist_ok=True)
    with open(pck, 'r+b') as f:
        base, entries = P.read_dir(f)
        n = 0
        for path in sorted(entries):
            if not (path.startswith(prefix) and path.endswith('.ogg.import')):
                continue
            _, off, size = entries[path]
            f.seek(base + off)
            text = f.read(size).decode()
            m = re.search(r'^path="([^"]+)"', text, re.M)
            real = m.group(1)
            assert real in entries, real
            name = os.path.basename(real).rsplit('.', 1)[0]  # "<file>.ogg-<hash>"
            tres = (
                '[gd_resource type="AudioStream" load_steps=2 format=3]\n\n'
                f'[ext_resource type="Script" path="{script_path}" id="1"]\n\n'
                '[resource]\n'
                'script = ExtResource("1")\n'
                f'real_path = "{real}"\n'
            )
            open(os.path.join(out_dir, name + '.tres'), 'w', encoding='utf-8', newline='\n').write(tres)
            new = text.replace(m.group(0), f'path="{target_dir.rstrip("/")}/{name}.tres"')
            new = new.replace('type="AudioStreamOggVorbis"', 'type="AudioStream"')
            P.replace(f, base, entries, path, new.encode())
            n += 1
    print('lazy music tracks:', n)


if __name__ == '__main__':
    if len(sys.argv) not in (5, 6):
        sys.exit(__doc__)
    main(*sys.argv[1:6])
