"""Texture conversion for Night in the Woods (Unity 5.6) on ARM handhelds, run in place on the device.

For every sharedassets*.assets / resources.assets file:
  * DXT1/DXT5/BC7/crunched/RGB(A)32/RGB24 Texture2Ds are decoded, scaled to half size and
    re-encoded as ASTC 6x6 (text and lettering textures stay full size at ASTC 4x4, textures of
    128 px or less keep their size). Mali and most other handheld GPUs have no DXT support.
  * A new .resS is written for each file, streamed to disk so memory use stays low.
  * Sprites that use a scaled texture in the same file get their pixel-space fields scaled.
    Sprites whose texture lives in another file are fixed by fix_cross_file_sprites() once every
    file is done, using the original sizes recorded here (no copy of the original game needed).
"""
import os, re
from PIL import Image
import astc_encoder as AE
import UnityPy
from UnityPy.enums import TextureFormat as TF

SOURCE_FORMATS = {TF.DXT1, TF.DXT5, TF.BC7, TF.DXT1Crunched, TF.DXT5Crunched,
                  TF.RGBA32, TF.ARGB32, TF.BGRA32, TF.RGB24}
NO_ALPHA = {TF.DXT1, TF.DXT1Crunched, TF.RGB24}
ASTC_FORMATS = {(4, 4): (TF.ASTC_RGB_4x4, TF.ASTC_RGBA_4x4), (6, 6): (TF.ASTC_RGB_6x6, TF.ASTC_RGBA_6x6)}

SCALE = 0.5
BLOCK, TEXT_BLOCK = (6, 6), (4, 4)
MIN_SIDE = 32         # don't downscale below this many pixels
MIN_CONVERT = 8       # leave textures smaller than this untouched
KEEP_MAX = 128        # don't downscale textures whose longest side is at most this
TEXT_RE = re.compile(r'(?i)dialogue_|text(?!ure)|letter|font|glyph')


def make_contexts(threads):
    cfg = lambda b: AE.ASTCConfig(AE.ASTCProfile.LDR, b[0], b[1], block_z=1, quality=AE.ASTCQualityPreset.FAST,
                                  flags=AE.ASTCConfigFlags.USE_DECODE_UNORM8)
    return {b: AE.ASTCContext(cfg(b), threads=threads) for b in (BLOCK, TEXT_BLOCK)}


def is_lut(t):
    w, h = t['m_Width'], t['m_Height']
    return 'lut' in t['m_Name'].lower() or (h >= 8 and w == h * h)


def encode(ctx, img):
    """img: PIL RGBA top-down -> ASTC blocks in Unity's bottom-up row order."""
    img = img.convert('RGBA').transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    astc_img = AE.ASTCImage(AE.ASTCType.U8, img.width, img.height, 1, img.tobytes())
    return ctx.compress(astc_img, AE.ASTCSwizzle.from_str('RGBA'))


def scaled_dims(w, h):
    if min(w, h) * SCALE < MIN_SIDE or max(w, h) <= KEEP_MAX:
        return w, h
    return max(1, round(w * SCALE)), max(1, round(h * SCALE))


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


def convert_file(path, ctxs, stats):
    """Convert one assets file. Writes <file>.tmp and <file>.resS.tmp and returns
    {path_id: [old_w, old_h, new_w, new_h]} for scaled textures, or None if nothing changed.
    The caller commits the .tmp files (see install.py)."""
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
            fmt = TF(t['m_TextureFormat'])
            convert = (fmt in SOURCE_FORMATS and not t['m_IsReadable'] and not is_lut(t)
                       and t['m_TextureDimension'] == 2 and t['m_ImageCount'] == 1
                       and min(t['m_Width'], t['m_Height']) >= MIN_CONVERT)
            if convert:
                img = o.read().image                          # decoded, top-down
                text = bool(TEXT_RE.search(t['m_Name']))
                block = TEXT_BLOCK if text else BLOCK
                w, h = (t['m_Width'], t['m_Height']) if text else scaled_dims(t['m_Width'], t['m_Height'])
                if (w, h) != img.size:
                    img = img.resize((w, h), Image.Resampling.LANCZOS)
                    scaled[pid] = [t['m_Width'], t['m_Height'], w, h]
                blob = encode(ctxs[block], img)
                del img
                rgb_fmt, rgba_fmt = ASTC_FORMATS[block]
                t['m_TextureFormat'] = int(rgb_fmt if fmt in NO_ALPHA else rgba_fmt)
                t['m_Width'], t['m_Height'] = w, h
                t['m_MipCount'] = 1
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


def fix_cross_file_sprites(path, scaled_by_file):
    """Scale sprites in `path` whose texture lives in another assets file that was scaled.
    scaled_by_file: {file basename: {str(path_id): [old_w, old_h, new_w, new_h]}}.
    Writes <file>.tmp and returns the number of sprites fixed (0 = nothing written)."""
    env = UnityPy.load(path)
    sf = list(env.files.values())[0]
    touched = []
    for pid, o in sf.objects.items():
        if o.type.name != 'Sprite':
            continue
        s = o.read_typetree()
        tp = s['m_RD']['texture']
        if not tp['m_FileID']:
            continue
        tfile = os.path.basename(sf.externals[tp['m_FileID'] - 1].path)
        dims = scaled_by_file.get(tfile, {}).get(str(tp['m_PathID']))
        if not dims:
            continue
        ow, oh, nw, nh = dims
        scale_sprite(s, nw / ow, nh / oh)
        touched.append((o, s))
    if not touched:
        return 0
    for o, s in touched:
        o.save_typetree(s)
    with open(path + '.tmp', 'wb') as f:
        f.write(sf.save()); f.flush(); os.fsync(f.fileno())
    return len(touched)
