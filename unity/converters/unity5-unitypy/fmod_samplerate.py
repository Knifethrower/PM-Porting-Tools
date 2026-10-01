"""Set the FMOD Unity integration's default output sample rate (FMODStudioSettings asset).

dArkOS mixes at 44.1 kHz (dmix "ddmix", 1024-frame periods). FMOD defaults to 48 kHz, so ALSA's plug
layer resamples and hands FMOD 1114-frame periods, which FMOD 1.09 cannot handle. Mixing at 44100
gives FMOD exactly the 1024-frame periods it asks for.

Usage: python fmod_samplerate.py <Data dir> [rate]      (default 44100; 0 = FMOD default)
"""
import gc, os, struct, sys
import UnityPy

PLATFORM_DEFAULT = 2


def find_samplerate_entry(raw):
    """SpeakerModeSettings is followed by SampleRateSettings; both are lists of (platform, value)
    int pairs. Locate the SampleRate list via the speaker-mode list, which holds stereo (3) values."""
    for p in range(0, len(raw) - 8, 4):
        n, = struct.unpack_from('<i', raw, p)
        if not 1 <= n <= 8:
            continue
        pairs = [struct.unpack_from('<ii', raw, p + 4 + 8 * k) for k in range(n)]
        if not all(0 <= plat < 21 and 0 <= val <= 7 for plat, val in pairs) or \
           not any(val == 3 for _, val in pairs):
            continue                                          # not the speaker-mode list
        q = p + 4 + 8 * n
        m, = struct.unpack_from('<i', raw, q)
        if not 0 <= m <= 8:
            continue
        entries = [struct.unpack_from('<ii', raw, q + 4 + 8 * k) for k in range(m)]
        if all(0 <= plat < 21 and val in (0, 8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000)
               for plat, val in entries):
            return q, entries
    return None, None


def patched_bytes(path, rate):
    env = UnityPy.load(path)
    sf = list(env.files.values())[0]
    target = None
    for pid, o in sf.objects.items():
        if o.type.name == 'MonoBehaviour' and b'FMODStudioSettings' in o.get_raw_data()[:80]:
            target = o
            break
    if target is None:
        sys.exit('FMODStudioSettings not found')
    raw = bytearray(target.get_raw_data())
    q, entries = find_samplerate_entry(raw)
    if q is None:
        sys.exit('SampleRateSettings list not recognised; not patching')
    print('SampleRateSettings before:', entries)
    for k, (plat, val) in enumerate(entries):
        if plat == PLATFORM_DEFAULT:
            struct.pack_into('<i', raw, q + 4 + 8 * k + 4, rate)
            break
    else:
        sys.exit('no Default entry in SampleRateSettings; not patching')
    target.set_raw_data(bytes(raw))
    print('SampleRateSettings after: ', find_samplerate_entry(bytes(raw))[1])
    return sf.save()


def main():
    data_dir = sys.argv[1]
    rate = int(sys.argv[2]) if len(sys.argv) > 2 else 44100
    path = os.path.join(data_dir, 'resources.assets')
    data = patched_bytes(path, rate)
    gc.collect()                      # Windows: release UnityPy's handle before replacing the file
    with open(path + '.tmp', 'wb') as f:
        f.write(data)
    os.replace(path + '.tmp', path)


if __name__ == '__main__':
    main()
