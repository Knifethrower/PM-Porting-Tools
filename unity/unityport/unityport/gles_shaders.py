"""Add GLES3 shader variants to a Unity 5.6 build that only ships OpenGLCore/Vulkan shaders.

Each OpenGLCore subprogram (HLSLcc-generated GLSL 150) is rewritten as GLSL ES 3.00 and
stored as a new GLES3Plus platform blob, and every GLCore SerializedSubProgram gets a GLES3
twin pointing at it. The player can then run with -force-gles30 on GLES-only drivers.

Usage: python gles_shaders.py <Data dir> [--dry-run] [--dump DIR]
Assets are patched in place, so point it at a copy of the game.
"""
import argparse, copy, glob, os, re, struct, sys
import lz4.block
import UnityPy

PLATFORM_GLCORE, PLATFORM_GLES3PLUS = 15, 9
GPU_GLCORE_TYPES = {6, 7, 8}          # GLCore32 / GLCore41 / GLCore43
GPU_GLES3 = 4


# ---------------------------------------------------------------- blob format (5.5 on)

def _align4(n):
    return (n + 3) & ~3


def parse_program_blob(data):
    """Return list of entries; each is (head, code, tail) so that head+len+code+pad+tail == entry."""
    count, = struct.unpack_from('<i', data)
    entries = []
    for i in range(count):
        off, length = struct.unpack_from('<ii', data, 4 + 8 * i)
        e = data[off:off + length]
        p = 4 + 4 + 12 + 4                             # version, type, stats, 5.5+ field
        nkw, = struct.unpack_from('<i', e, p); p += 4
        for _ in range(nkw):
            n, = struct.unpack_from('<i', e, p); p = _align4(p + 4 + n)
        clen, = struct.unpack_from('<i', e, p)
        head, code = e[:p], e[p + 4:p + 4 + clen]
        tail = e[_align4(p + 4 + clen):]
        entries.append([head, code, tail])
    return entries


def build_program_blob(entries):
    bodies = []
    for head, code, tail in entries:
        b = head + struct.pack('<i', len(code)) + code
        b += b'\0' * (_align4(len(b)) - len(b))
        bodies.append(b + tail)
    out = bytearray(struct.pack('<i', len(entries)))
    off = 4 + 8 * len(entries)
    for b in bodies:
        out += struct.pack('<ii', off, len(b)); off += len(b)
    for b in bodies:
        out += b
    return bytes(out)


def retype_head(head, gpu_type):
    return head[:4] + struct.pack('<i', gpu_type) + head[8:]


# ---------------------------------------------------------------- GLSL 150 -> GLSL ES 3.00

ES_PRECISION = {
    'VERTEX': 'precision highp float;\nprecision highp int;\n',
    'FRAGMENT': 'precision highp float;\nprecision highp int;\n',
}
# sampler types without a default precision in ES 3.00
ES_SAMPLERS = ('sampler3D', 'sampler2DShadow', 'samplerCubeShadow', 'sampler2DArray',
               'sampler2DArrayShadow', 'isampler2D', 'usampler2D', 'isampler3D', 'usampler3D')


def convert_stage(src, stage):
    lines = []
    for line in src.split('\n'):
        s = line.strip()
        if s.startswith('#extension GL_ARB_explicit_attrib_location') or \
           s.startswith('#extension GL_ARB_shader_bit_encoding'):
            continue
        lines.append(line)
    src = '\n'.join(lines)
    pre = ES_PRECISION[stage] + ''.join(f'precision highp {t};\n' for t in ES_SAMPLERS if re.search(rf'\b{t}\b', src))
    src = re.sub(r'#version (?:150|330)[^\n]*\n', '#version 300 es\n' + pre, src, count=1)
    src = re.sub(r'\bnoperspective\s+', '', src)          # not in ES 3.00
    return src


def split_stages(text):
    """Yield (stage, start, end) for each '#ifdef VERTEX|FRAGMENT' block body; the body runs to
    the last '#endif' before the next stage marker, so nested #if blocks are kept intact."""
    marks = [(m.group(1), m.end()) for m in re.finditer(r'#ifdef (VERTEX|FRAGMENT)\n', text)]
    for i, (stage, start) in enumerate(marks):
        limit = marks[i + 1][1] - len(f'#ifdef {marks[i + 1][0]}\n') if i + 1 < len(marks) else len(text)
        end = text.rfind('#endif', start, limit)
        if end == -1:
            raise ValueError(f'unterminated {stage} block')
        yield stage, start, end


def convert_program(code):
    """code holds '#ifdef VERTEX ... #endif #ifdef FRAGMENT ... #endif' (either part may be absent)."""
    text = code.decode('utf-8')
    # 150 = regular HLSLcc output, 330 = instancing/UBO variants; 4xx (DX11-only effects) stay as-is
    if not re.search(r'#version (?:150|330)\b', text):
        return code
    parts, pos = [], 0
    for stage, start, end in split_stages(text):
        parts += [text[pos:start], convert_stage(text[start:end], stage)]
        pos = end
    if not parts:
        raise ValueError('no stage blocks found')
    parts.append(text[pos:])
    return ''.join(parts).encode('utf-8')


# ---------------------------------------------------------------- shader asset patching

def blobs_of(t):
    blob = bytes(t['compressedBlob'])
    return [lz4.block.decompress(blob[o:o + c], uncompressed_size=d)
            for o, c, d in zip(t['offsets'], t['compressedLengths'], t['decompressedLengths'])]


def strip_gles3(t):
    """Remove a GLES3Plus platform added by an earlier run (this build ships none of its own)."""
    plats = list(t['platforms'])
    if PLATFORM_GLES3PLUS not in plats:
        return False
    i = plats.index(PLATFORM_GLES3PLUS)
    raw = bytes(t['compressedBlob'])
    if i == len(plats) - 1:                      # appended last: drop its bytes too
        raw = raw[:t['offsets'][i]]
    for key in ('platforms', 'offsets', 'compressedLengths', 'decompressedLengths'):
        v = list(t[key]); del v[i]; t[key] = v
    t['compressedBlob'] = list(raw) if isinstance(t['compressedBlob'], list) else raw
    for sub in t['m_ParsedForm']['m_SubShaders']:
        for p in sub['m_Passes']:
            for key in ('progVertex', 'progFragment', 'progGeometry', 'progHull', 'progDomain'):
                p[key]['m_SubPrograms'] = [s for s in p[key]['m_SubPrograms'] if s['m_GpuProgramType'] != GPU_GLES3]
    return True


def patch_shader_tree(t, dump=None, redo=False):
    """Returns number of programs converted, or 0 if the shader needs no change."""
    if redo:
        strip_gles3(t)
    plats = list(t['platforms'])
    if PLATFORM_GLCORE not in plats or PLATFORM_GLES3PLUS in plats:
        return 0
    gl_index = plats.index(PLATFORM_GLCORE)
    entries = parse_program_blob(blobs_of(t)[gl_index])
    new = []
    for head, code, tail in entries:
        gpu_type, = struct.unpack_from('<i', head, 4)
        es = convert_program(code)
        new.append([retype_head(head, GPU_GLES3) if gpu_type in GPU_GLCORE_TYPES else head, es, tail])
        if dump is not None and es != code:
            dump.append(es.decode())
    es_blob = build_program_blob(new)
    comp = lz4.block.compress(es_blob, store_size=False, mode='high_compression')
    raw = bytes(t['compressedBlob'])
    t['platforms'] = plats + [PLATFORM_GLES3PLUS]
    t['offsets'] = list(t['offsets']) + [len(raw)]
    t['compressedLengths'] = list(t['compressedLengths']) + [len(comp)]
    t['decompressedLengths'] = list(t['decompressedLengths']) + [len(es_blob)]
    t['compressedBlob'] = list(raw + comp) if isinstance(t['compressedBlob'], list) else raw + comp
    # every GLCore subprogram gets a GLES3 twin using the same blob index in the new platform
    for sub in t['m_ParsedForm']['m_SubShaders']:
        for p in sub['m_Passes']:
            for key in ('progVertex', 'progFragment', 'progGeometry', 'progHull', 'progDomain'):
                sps = p[key]['m_SubPrograms']
                sps.extend(dict(copy.deepcopy(s), m_GpuProgramType=GPU_GLES3)
                           for s in list(sps) if s['m_GpuProgramType'] in GPU_GLCORE_TYPES)
    return len(new)


def selftest_roundtrip(t):
    gl = blobs_of(t)[list(t['platforms']).index(PLATFORM_GLCORE)]
    # Unity lays entries out in arbitrary order, so compare parsed contents, not bytes
    parsed = parse_program_blob(gl)
    assert parse_program_blob(build_program_blob(parsed)) == parsed, 'blob round-trip mismatch'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('data_dir')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--dump', help='write converted GLSL here for validation')
    ap.add_argument('--redo', action='store_true', help='rebuild GLES3 variants added by an earlier run')
    a = ap.parse_args()
    files = sorted(glob.glob(os.path.join(a.data_dir, '*.assets'))) + \
        sorted(glob.glob(os.path.join(a.data_dir, 'Resources', '*')))
    dump = [] if a.dump else None
    total_sh = total_pr = 0
    for f in files:
        try:
            env = UnityPy.load(f)
        except Exception:
            continue
        changed = 0
        for o in env.objects:
            if o.type.name != 'Shader':
                continue
            t = o.read_typetree()
            if PLATFORM_GLCORE in t['platforms']:
                selftest_roundtrip(t)
            n = patch_shader_tree(t, dump, redo=a.redo)
            if n:
                o.save_typetree(t); changed += 1; total_pr += n
        if changed and not a.dry_run:
            data = list(env.files.values())[0].save()
            with open(f, 'wb') as fh:
                fh.write(data)
        if changed:
            total_sh += changed
            print(f'{os.path.relpath(f, a.data_dir)}: {changed} shaders', flush=True)
    print(f'patched {total_sh} shaders, {total_pr} programs{" (dry run)" if a.dry_run else ""}')
    if dump is not None:
        os.makedirs(a.dump, exist_ok=True)
        for i, src in enumerate(dump):
            for stage, start, end in split_stages(src):
                ext = 'vert' if stage == 'VERTEX' else 'frag'
                with open(os.path.join(a.dump, f'p{i:05d}.{ext}'), 'w', newline='\n') as fh:
                    fh.write(src[start:end])


if __name__ == '__main__':
    main()
