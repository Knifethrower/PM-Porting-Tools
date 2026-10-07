"""Audio conversion for Gone Home.

Gone Home ships its audio as uncompressed 16-bit PCM (1.8 GB) in FMOD FSB5 containers inside
*.resource files. For each assets file this:
  * switches long clips (music, ambience, commentary - 8 MB of PCM or more) that are loaded whole
    into memory to Streaming, so they are read from disk while they play instead of taking up to
    250 MB of RAM each;
  * with adpcm=True, re-encodes the PCM clips as IMA ADPCM (FMOD's FSB5 IMAADPCM codec, which
    Unity's built-in FMOD plays natively: Unity's "ADPCM" compression format). About 3.5x smaller
    (1.8 GB -> 0.5 GB) and cheap to decode. The encoder is imaadpcm/libimaadpcm.<arch>.so.

Writes <assets>.tmp and <resource>.tmp and returns the list of extra .tmp base paths for the
caller to commit (see install.py), or None when nothing changed.
"""
import ctypes, os, platform, struct
import UnityPy

STREAM_MIN = 8 << 20            # clips with at least this much PCM are streamed
FORMAT_PCM, FORMAT_ADPCM = 0, 2
FSB_PCM16, FSB_IMAADPCM = 2, 7
LOAD_DECOMPRESS, LOAD_COMPRESSED, LOAD_STREAMING = 0, 1, 2
CHUNK_FRAMES = 64 * 16384       # encode long clips in pieces (multiple of 64 frames)

_lib = None


def lib():
    global _lib
    if _lib is None:
        d = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'imaadpcm')
        if os.name == 'nt':
            name = 'imaadpcm.dll'
        else:
            name = 'libimaadpcm.aarch64.so' if platform.machine() in ('aarch64', 'arm64') else 'libimaadpcm.x86_64.so'
        _lib = ctypes.CDLL(os.path.join(d, name))
        _lib.ima_size.restype = ctypes.c_long
        _lib.ima_size.argtypes = [ctypes.c_long, ctypes.c_int]
        _lib.ima_encode.restype = ctypes.c_long
        _lib.ima_encode.argtypes = [ctypes.c_char_p, ctypes.c_long, ctypes.c_int, ctypes.c_char_p,
                                    ctypes.POINTER(ctypes.c_int)]
    return _lib


def parse_fsb5(head):
    """head: at least the FSB5 header + sample headers. -> dict or None if unsupported."""
    if head[:4] != b'FSB5':
        return None
    version, nsub, shs, nts, dsize, codec = struct.unpack_from('<6I', head, 4)
    base = 0x3c if version == 1 else 0x40
    if nsub != 1:
        return None
    mode, = struct.unpack_from('<Q', head, base)
    return dict(base=base, shs=shs, nts=nts, dsize=dsize, codec=codec,
                samples=(mode >> 34) & 0x3FFFFFFF, channels={0: 1, 1: 2, 2: 6, 3: 8}[(mode >> 5) & 3],
                data_off=((mode >> 7) & 0x07FFFFFF) << 5, extra=bool(mode & 1))


def encode_fsb(src, offset, size):
    """Read one PCM16 FSB5 from `src` at offset and return it re-encoded as IMA ADPCM
    (or None when the clip can't be converted)."""
    src.seek(offset)
    head = src.read(min(size, 4096))
    h = parse_fsb5(head)
    if not h or h['codec'] != FSB_PCM16 or h['channels'] not in (1, 2) or h['data_off'] != 0:
        return None
    if h['extra']:
        # extra chunk 0x01 (channel count override) would change the layout; don't touch those
        p = h['base'] + 8
        while p < h['base'] + h['shs']:
            flag, = struct.unpack_from('<I', head, p)
            if (flag >> 25) & 0x7F == 0x01:
                return None
            p += 4 + ((flag >> 1) & 0xFFFFFF)
            if not flag & 1:
                break
    ch, frames = h['channels'], h['samples']
    hdr_len = h['base'] + h['shs'] + h['nts']
    L = lib()
    out = bytearray()
    state = (ctypes.c_int * 4)(0, 0, 0, 0)
    src.seek(offset + hdr_len)
    done = 0
    while done < frames:
        n = min(CHUNK_FRAMES, frames - done)
        pcm = src.read(n * ch * 2)
        if len(pcm) < n * ch * 2:
            pcm += b'\0' * (n * ch * 2 - len(pcm))
        buf = ctypes.create_string_buffer(L.ima_size(n, ch))
        w = L.ima_encode(pcm, n, ch, buf, state)
        out += buf.raw[:w]
        done += n
    out += b'\0' * ((-len(out)) % 32)
    new_head = bytearray(head[:hdr_len])
    struct.pack_into('<I', new_head, 0x14, len(out))
    struct.pack_into('<I', new_head, 0x18, FSB_IMAADPCM)
    return bytes(new_head) + bytes(out)


def convert_file(path, adpcm, log=print):
    data_dir, base = os.path.split(path)
    env = UnityPy.load(path)
    sf = list(env.files.values())[0]
    clips = []
    for o in sf.objects.values():
        if o.type.name == 'AudioClip':
            clips.append((o, o.read_typetree()))
    if not clips:
        return None
    res_name = os.path.splitext(base)[0] + '.resource'
    res_path = os.path.join(data_dir, res_name)
    touched, streamed, encoded = {}, 0, 0
    size_in = size_out = 0

    # load type: long clips held in memory -> streamed
    for o, t in clips:
        pcm_bytes = t['m_Resource']['m_Size']
        if t['m_LoadType'] in (LOAD_DECOMPRESS, LOAD_COMPRESSED) and pcm_bytes >= STREAM_MIN:
            t['m_LoadType'] = LOAD_STREAMING
            streamed += 1
            touched[id(o)] = (o, t)

    extra = []
    if adpcm and os.path.exists(res_path):
        mine = sorted(((o, t) for o, t in clips
                       if os.path.basename(t['m_Resource']['m_Source']) == res_name and t['m_Resource']['m_Size']),
                      key=lambda ot: ot[1]['m_Resource']['m_Offset'])
        moved = {}                                 # old offset -> (new offset, size, converted)
        with open(res_path, 'rb') as src, open(res_path + '.tmp', 'wb') as dst:
            pos = 0
            for i, (o, t) in enumerate(mine):
                r = t['m_Resource']
                key = r['m_Offset']
                if key not in moved:
                    blob = None
                    if t['m_CompressionFormat'] == FORMAT_PCM and t['m_BitsPerSample'] == 16:
                        blob = encode_fsb(src, r['m_Offset'], r['m_Size'])
                    conv = blob is not None
                    if not conv:
                        src.seek(r['m_Offset']); blob = src.read(r['m_Size'])
                    pad = (-pos) % 32
                    dst.write(b'\0' * pad); pos += pad
                    dst.write(blob)
                    moved[key] = (pos, len(blob), conv)
                    pos += len(blob)
                    size_in += r['m_Size']; size_out += len(blob)
                    if i % 20 == 0 or r['m_Size'] > (32 << 20):
                        log(f'  audio: {res_name} {i + 1}/{len(mine)} clips, '
                            f'{size_in >> 20} -> {size_out >> 20} MB')
                new_off, new_size, conv = moved[key]
                r['m_Offset'], r['m_Size'] = new_off, new_size
                if conv:
                    t['m_CompressionFormat'] = FORMAT_ADPCM
                    encoded += 1
                touched[id(o)] = (o, t)
            dst.flush(); os.fsync(dst.fileno())
        extra.append(res_path)

    if not touched:
        return None
    for o, t in touched.values():
        o.save_typetree(t)
    with open(path + '.tmp', 'wb') as f:
        f.write(sf.save()); f.flush(); os.fsync(f.fileno())
    msg = f'audio: {base}: {streamed} clips streamed'
    if adpcm:
        msg += f', {encoded} re-encoded as ADPCM ({size_in >> 20} -> {size_out >> 20} MB)'
    log(msg)
    return extra
