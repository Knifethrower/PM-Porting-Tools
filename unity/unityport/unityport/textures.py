"""Step textures: DXT / BC7 / RGB(A) textures -> ASTC, run in place on the device.

Mali and most other handheld GPUs have no DXT (S3TC) support. Every streamed texture in a
supported source format is decoded and re-encoded as ASTC:
  * Scalable textures are scaled by textures.scale (never below min_side, never when the longest
    side is at most keep_max) and encoded with textures.block.
  * Sharp textures - names matching sharp_names, and whatever scale_files /
    scale_mipmapped_only exclude - keep their size (text stays legible; UI atlases such as NGUI's
    store sprite rectangles in pixels and must never be resized) and use sharp_block
    (sharp_block_dxt1 for DXT1 sources, which need fewer bits).
  * mipmaps = "keep": a new full ASTC mip chain where the original had mips; "none": base only.
  * Readable textures, LUTs, cubemaps/arrays and other formats (Alpha8, ARGB4444, ...) are left
    alone.
A new .resS is written for each assets file, streamed to disk so memory use stays low. Sprites
in the same file that use a scaled texture are rescaled here; sprites in other files are fixed by
the sprites step from the sizes recorded in the state file.
"""
import fnmatch, gc, os, re, sys

SOURCE = ('DXT1', 'DXT5', 'BC7', 'DXT1Crunched', 'DXT5Crunched', 'RGBA32', 'ARGB32', 'BGRA32', 'RGB24')
NO_ALPHA = ('DXT1', 'DXT1Crunched', 'RGB24')


def make_contexts(blocks, quality, threads):
    import astc_encoder as AE
    preset = {'fast': AE.ASTCQualityPreset.FAST, 'medium': AE.ASTCQualityPreset.MEDIUM,
              'fastest': AE.ASTCQualityPreset.FASTEST, 'thorough': AE.ASTCQualityPreset.THOROUGH}[quality]
    cfg = lambda b: AE.ASTCConfig(AE.ASTCProfile.LDR, b[0], b[1], block_z=1, quality=preset,
                                  flags=AE.ASTCConfigFlags.USE_DECODE_UNORM8)
    return {b: AE.ASTCContext(cfg(b), threads=threads) for b in blocks}


def astc_formats(block):
    from UnityPy.enums import TextureFormat as TF
    x, y = block
    return getattr(TF, f'ASTC_RGB_{x}x{y}'), getattr(TF, f'ASTC_RGBA_{x}x{y}')


def is_lut(t):
    w, h = t['m_Width'], t['m_Height']
    return 'lut' in t['m_Name'].lower() or (h >= 8 and w == h * h)


def encode(ctx, img):
    """img: PIL RGBA top-down -> ASTC blocks in Unity's bottom-up row order."""
    import astc_encoder as AE
    from PIL import Image
    img = img.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    astc_img = AE.ASTCImage(AE.ASTCType.U8, img.width, img.height, 1, img.tobytes())
    return ctx.compress(astc_img, AE.ASTCSwizzle.from_str('RGBA'))


def encode_chain(ctx, img, mips):
    """Base level + (mips - 1) box-filtered levels, largest first, as Unity stores them."""
    from PIL import Image
    out, level = [encode(ctx, img)], img
    for _ in range(mips - 1):
        w, h = max(1, level.width // 2), max(1, level.height // 2)
        level = level.resize((w, h), Image.Resampling.BOX)
        out.append(encode(ctx, level))
    return b''.join(out)


def full_mip_count(w, h):
    return max(w, h).bit_length()


def scale_sprite(s, sx, sy):
    for key, fx, fy in (('m_Rect', ('x', 'width'), ('y', 'height')), ('m_Offset', ('x',), ('y',))):
        for k in fx: s[key][k] *= sx
        for k in fy: s[key][k] *= sy
    b = s['m_Border']; b['x'] *= sx; b['z'] *= sx; b['y'] *= sy; b['w'] *= sy
    s['m_PixelsToUnits'] *= sx
    rd = s['m_RD']
    for k in ('x', 'width'): rd['textureRect'][k] *= sx
    for k in ('y', 'height'): rd['textureRect'][k] *= sy
    rd['textureRectOffset']['x'] *= sx; rd['textureRectOffset']['y'] *= sy
    if rd['atlasRectOffset']['x'] != -1 or rd['atlasRectOffset']['y'] != -1:
        rd['atlasRectOffset']['x'] *= sx; rd['atlasRectOffset']['y'] *= sy
    u = rd['uvTransform']; u['x'] *= sx; u['y'] *= sx; u['z'] *= sy; u['w'] *= sy


class Planner:
    def __init__(self, c):
        self.c = c
        self.sharp_re = re.compile('(?i)' + c['sharp_names']) if c['sharp_names'] else None
        self.block = tuple(c['block'])
        self.sharp_block = tuple(c['sharp_block'])
        self.sharp_dxt1 = tuple(c['sharp_block_dxt1'] or c['sharp_block'])

    def blocks(self):
        return sorted({self.block, self.sharp_block, self.sharp_dxt1})

    def scaled_dims(self, w, h):
        s = self.c['scale']
        if min(w, h) * s < self.c['min_side'] or max(w, h) <= self.c['keep_max']:
            return w, h
        return max(1, round(w * s)), max(1, round(h * s))

    def plan(self, path, t, fmt_name):
        """-> (block, new_w, new_h, mip_count) for a texture to convert, or None to keep it."""
        c = self.c
        w, h, mips = t['m_Width'], t['m_Height'], t['m_MipCount']
        if (fmt_name not in SOURCE or t['m_IsReadable'] or is_lut(t) or t['m_TextureDimension'] != 2
                or t['m_ImageCount'] != 1 or min(w, h) < c['min_convert']):
            return None
        scalable = not (self.sharp_re and self.sharp_re.search(t['m_Name']))
        if c['scale_files'] and not fnmatch.fnmatch(os.path.basename(path), c['scale_files']):
            scalable = False
        if c['scale_mipmapped_only'] and mips <= 1:
            scalable = False
        if scalable:
            block = self.block
            w, h = self.scaled_dims(w, h)
        else:
            block = self.sharp_dxt1 if fmt_name in ('DXT1', 'DXT1Crunched') else self.sharp_block
        n = full_mip_count(w, h) if c['mipmaps'] == 'keep' and mips > 1 else 1
        return block, w, h, n


def convert_file(path, planner, ctxs, stats):
    """Convert one assets file. Writes <file>.tmp and <file>.resS.tmp and returns
    {path_id: [old_w, old_h, new_w, new_h]} for scaled textures, or None if nothing changed."""
    import UnityPy
    from PIL import Image
    from UnityPy.enums import TextureFormat as TF
    data_dir, base = os.path.split(path)
    res_name = base + '.resS'
    old_res_path = os.path.join(data_dir, res_name)
    if not os.path.exists(old_res_path):
        return None
    env = UnityPy.load(path)
    sf = list(env.files.values())[0]
    scaled, touched, converted = {}, [], 0
    tmp_res = old_res_path + '.tmp'
    with open(old_res_path, 'rb') as old_res, open(tmp_res, 'wb') as new_res:
        pos = 0
        for pid, o in sf.objects.items():
            if o.type.name != 'Texture2D':
                continue
            t = o.read_typetree()
            sd = t.get('m_StreamData') or {}
            if not sd.get('size') or os.path.basename(sd.get('path', '')) != res_name:
                continue
            try:
                fmt = TF(t['m_TextureFormat'])
                fmt_name = fmt.name
            except ValueError:
                fmt_name = None
            p = planner.plan(path, t, fmt_name)
            if p:
                block, w, h, mips = p
                img = o.read().image.convert('RGBA')          # decoded, top-down
                if (w, h) != img.size:
                    img = img.resize((w, h), Image.Resampling.LANCZOS)
                    scaled[pid] = [t['m_Width'], t['m_Height'], w, h]
                blob = encode_chain(ctxs[block], img, mips)
                del img
                rgb_fmt, rgba_fmt = astc_formats(block)
                t['m_TextureFormat'] = int(rgb_fmt if fmt_name in NO_ALPHA else rgba_fmt)
                t['m_Width'], t['m_Height'], t['m_MipCount'] = w, h, mips
                converted += 1
            else:
                old_res.seek(sd['offset']); blob = old_res.read(sd['size'])
            pad = (-pos) % 16
            new_res.write(b'\0' * pad); pos += pad
            stats['in'] += sd['size']; stats['out'] += len(blob)
            t['m_StreamData'] = {'offset': pos, 'size': len(blob), 'path': sd['path']}
            t['m_CompleteImageSize'] = len(blob)
            if 'image data' in t: t['image data'] = b''
            new_res.write(blob); pos += len(blob)
            touched.append((o, t))
        new_res.flush(); os.fsync(new_res.fileno())
    stats['textures'] += converted
    if not converted:
        os.remove(tmp_res)                  # already converted (or nothing to do): leave as is
        return None
    for pid, o in sf.objects.items():       # sprites that use scaled textures from this same file
        if o.type.name != 'Sprite' or not scaled:
            continue
        s = o.read_typetree()
        tp = s['m_RD']['texture']
        if tp['m_FileID'] == 0 and tp['m_PathID'] in scaled:
            ow, oh, nw, nh = scaled[tp['m_PathID']]
            scale_sprite(s, nw / ow, nh / oh)
            touched.append((o, s)); stats['sprites'] += 1
    for o, t in touched:
        o.save_typetree(t)
    with open(path + '.tmp', 'wb') as f:
        f.write(sf.save()); f.flush(); os.fsync(f.fileno())
    return scaled


def run(inst):
    c = inst.cfg['textures']
    planner = Planner(c)
    files = inst.files(c['files'], c['sort'])
    todo = [f for f in files if not inst.file_done('textures', f)]
    size = lambda f: os.path.getsize(f + '.resS') if os.path.exists(f + '.resS') else 0
    ctxs = make_contexts(planner.blocks(), c['quality'], os.cpu_count() or 4)
    stats = dict(textures=0, sprites=0, **{'in': 0, 'out': 0})
    for f in todo:
        inst.need_space(size(f) + (200 << 20), f'convert {os.path.basename(f)}')
        inst.log(f'textures: converting {os.path.basename(f)} ({size(f) >> 20} MB)')
        scaled = convert_file(f, planner, ctxs, stats)
        gc.collect()
        if scaled is None:
            inst.skip_file('textures', f)
        else:
            inst.state['scaled'][os.path.basename(f)] = {str(k): v for k, v in scaled.items()}
            inst.commit('textures', f, extra_tmp=(f + '.resS',))
        n = len(inst.state['files'].get('textures', []))
        inst.log(f'textures: {os.path.basename(f)} done ({n}/{len(files)} files, '
                 f'{stats["textures"]} textures converted, {stats["in"] >> 20} -> {stats["out"] >> 20} MB)')
    inst.log(f'textures: {stats["textures"]} converted, {stats["sprites"]} sprites rescaled')
