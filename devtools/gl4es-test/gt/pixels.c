// Pixel transfer: texture uploads (every format/type/internal format), sub-image uploads with unpack
// state, glReadPixels and glGetTexImage with every format/type and pack state, compressed uploads,
// texture parameters. The GL_UNSIGNED_SHORT upload bug (pixel.c:159) is tex_upload/ALPHA16/ALPHA/USHORT.
#include "gt.h"

typedef struct { GLenum e; const char *n; } en_t;
#define E(x) { GL_##x, #x }
static const en_t FMTS[] = { E(RED), E(GREEN), E(BLUE), E(ALPHA), E(RGB), E(BGR), E(RGBA), E(BGRA), E(LUMINANCE), E(LUMINANCE_ALPHA) };
static const en_t UTYPES[] = { E(UNSIGNED_BYTE), E(BYTE), E(UNSIGNED_SHORT), E(SHORT), E(UNSIGNED_INT), E(INT), E(FLOAT), E(HALF_FLOAT_ARB) };
static const en_t PTYPES3[] = { E(UNSIGNED_BYTE_3_3_2), E(UNSIGNED_BYTE_2_3_3_REV), E(UNSIGNED_SHORT_5_6_5), E(UNSIGNED_SHORT_5_6_5_REV) };
static const en_t PTYPES4[] = { E(UNSIGNED_SHORT_4_4_4_4), E(UNSIGNED_SHORT_4_4_4_4_REV), E(UNSIGNED_SHORT_5_5_5_1),
    E(UNSIGNED_SHORT_1_5_5_5_REV), E(UNSIGNED_INT_8_8_8_8), E(UNSIGNED_INT_8_8_8_8_REV), E(UNSIGNED_INT_10_10_10_2),
    E(UNSIGNED_INT_2_10_10_10_REV) };
// internal formats with the tolerance their precision allows (8-bit readback)
typedef struct { GLenum e; const char *n; int tol; } if_t;
#define I(x, t) { GL_##x, #x, t }
static const if_t IFMTS[] = {
    I(ALPHA, 2), I(ALPHA4, 18), I(ALPHA8, 2), I(ALPHA12, 2), I(ALPHA16, 2),
    I(LUMINANCE, 2), I(LUMINANCE4, 18), I(LUMINANCE8, 2), I(LUMINANCE16, 2),
    I(LUMINANCE_ALPHA, 2), I(LUMINANCE4_ALPHA4, 18), I(LUMINANCE6_ALPHA2, 86), I(LUMINANCE8_ALPHA8, 2), I(LUMINANCE16_ALPHA16, 2),
    I(INTENSITY, 2), I(INTENSITY4, 18), I(INTENSITY8, 2), I(INTENSITY16, 2),
    I(RGB, 2), I(R3_G3_B2, 86), I(RGB4, 18), I(RGB5, 9), I(RGB8, 2), I(RGB10, 2), I(RGB16, 2),
    I(RGBA, 2), I(RGBA2, 86), I(RGBA4, 18), I(RGB5_A1, 128), I(RGBA8, 2), I(RGB10_A2, 86), I(RGBA16, 2),
    { 1, "1", 2 }, { 2, "2", 2 }, { 3, "3", 2 }, { 4, "4", 2 },
};
#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

int gt_fmt_ncomp(GLenum f) {
    switch (f) { case GL_RGB: case GL_BGR: return 3; case GL_RGBA: case GL_BGRA: return 4; case GL_LUMINANCE_ALPHA: return 2; }
    return 1;
}
int gt_type_size(GLenum t) {
    switch (t) { case GL_UNSIGNED_BYTE: case GL_BYTE: return 1; case GL_UNSIGNED_SHORT: case GL_SHORT: case GL_HALF_FLOAT_ARB: return 2; }
    return 4;
}
int gt_type_packed(GLenum t, int *f, int *nf, int *lsb) {
    static const struct { GLenum t; int bytes, n, f[4], lsb; } P[] = {
        { GL_UNSIGNED_BYTE_3_3_2, 1, 3, { 3, 3, 2 }, 0 }, { GL_UNSIGNED_BYTE_2_3_3_REV, 1, 3, { 3, 3, 2 }, 1 },
        { GL_UNSIGNED_SHORT_5_6_5, 2, 3, { 5, 6, 5 }, 0 }, { GL_UNSIGNED_SHORT_5_6_5_REV, 2, 3, { 5, 6, 5 }, 1 },
        { GL_UNSIGNED_SHORT_4_4_4_4, 2, 4, { 4, 4, 4, 4 }, 0 }, { GL_UNSIGNED_SHORT_4_4_4_4_REV, 2, 4, { 4, 4, 4, 4 }, 1 },
        { GL_UNSIGNED_SHORT_5_5_5_1, 2, 4, { 5, 5, 5, 1 }, 0 }, { GL_UNSIGNED_SHORT_1_5_5_5_REV, 2, 4, { 5, 5, 5, 1 }, 1 },
        { GL_UNSIGNED_INT_8_8_8_8, 4, 4, { 8, 8, 8, 8 }, 0 }, { GL_UNSIGNED_INT_8_8_8_8_REV, 4, 4, { 8, 8, 8, 8 }, 1 },
        { GL_UNSIGNED_INT_10_10_10_2, 4, 4, { 10, 10, 10, 2 }, 0 }, { GL_UNSIGNED_INT_2_10_10_10_REV, 4, 4, { 10, 10, 10, 2 }, 1 },
    };
    for (int i = 0; i < N(P); i++) if (P[i].t == t) {
        if (f) { memcpy(f, P[i].f, sizeof P[i].f); *nf = P[i].n; *lsb = P[i].lsb; }
        return P[i].bytes;
    }
    return 0;
}
int gt_pixel_bytes(GLenum fmt, GLenum type) {
    int p = gt_type_packed(type, NULL, NULL, NULL);
    return p ? p : gt_fmt_ncomp(fmt) * gt_type_size(type);
}
int gt_type_bits(GLenum t) {
    int f[4], nf, lsb, m = 99;
    if (gt_type_packed(t, f, &nf, &lsb)) { for (int i = 0; i < nf; i++) if (f[i] < m) m = f[i]; return m; }
    return t == GL_UNSIGNED_BYTE || t == GL_BYTE ? 8 : 16;
}
// GL 2.1 pixel store addressing (spec 3.6.4)
size_t gt_image_size(int w, int h, GLenum fmt, GLenum type, int align, int rowlen, int skippix, int skiprows, size_t *stride) {
    int l = rowlen > 0 ? rowlen : w;
    int pb = gt_pixel_bytes(fmt, type);
    int s = gt_type_packed(type, NULL, NULL, NULL) ? pb : gt_type_size(type);
    size_t row = (size_t)l * pb;
    if (s < align) row = (size_t)align * (((size_t)s * gt_fmt_ncomp(fmt) * l + align - 1) / align);
    if (gt_type_packed(type, NULL, NULL, NULL) && pb < align) row = (size_t)align * (((size_t)pb * l + align - 1) / align);
    if (stride) *stride = row;
    return row * (size_t)(skiprows + h) + (size_t)skippix * pb;
}
static float half_to_f(uint16_t h) {
    int s = h >> 15, e = (h >> 10) & 31, m = h & 1023; float v;
    if (e == 0) v = ldexpf((float)m, -24); else if (e == 31) v = m ? NAN : INFINITY; else v = ldexpf((float)(m | 1024), e - 25);
    return s ? -v : v;
}
static uint16_t f_to_half(float f) {
    uint32_t x; memcpy(&x, &f, 4);
    uint32_t s = (x >> 16) & 0x8000; int e = ((x >> 23) & 255) - 127 + 15; uint32_t m = x & 0x7fffff;
    if (e <= 0) return (uint16_t)s; if (e >= 31) return (uint16_t)(s | 0x7c00);
    return (uint16_t)(s | (e << 10) | (m >> 13));
}
int gt_decode(GLenum fmt, GLenum type, const uint8_t *p, float *out) {
    int n = gt_fmt_ncomp(fmt), f[4], nf, lsb, bytes = gt_type_packed(type, f, &nf, &lsb);
    if (bytes) {
        uint32_t v = bytes == 1 ? p[0] : bytes == 2 ? *(const uint16_t *)p : *(const uint32_t *)p;
        int total = bytes * 8, pos = lsb ? 0 : total;
        for (int i = 0; i < nf; i++) {
            int sh = lsb ? pos : pos - f[i];
            out[i] = (float)((v >> sh) & ((1u << f[i]) - 1)) / (float)((1u << f[i]) - 1);
            pos = lsb ? pos + f[i] : pos - f[i];
        }
        return nf;
    }
    for (int i = 0; i < n; i++) switch (type) {
        case GL_UNSIGNED_BYTE: out[i] = p[i] / 255.f; break;
        case GL_BYTE: out[i] = fmaxf(((const int8_t *)p)[i] / 127.f, -1); break;
        case GL_UNSIGNED_SHORT: out[i] = ((const uint16_t *)p)[i] / 65535.f; break;
        case GL_SHORT: out[i] = fmaxf(((const int16_t *)p)[i] / 32767.f, -1); break;
        case GL_UNSIGNED_INT: out[i] = (float)(((const uint32_t *)p)[i] / 4294967295.0); break;
        case GL_INT: out[i] = (float)fmax(((const int32_t *)p)[i] / 2147483647.0, -1); break;
        case GL_FLOAT: out[i] = ((const float *)p)[i]; break;
        case GL_HALF_FLOAT_ARB: out[i] = half_to_f(((const uint16_t *)p)[i]); break;
    }
    return n;
}
static int valid_combo(GLenum fmt, GLenum type) {
    if (gt_type_packed(type, NULL, NULL, NULL)) {
        int f[4], nf, lsb; gt_type_packed(type, f, &nf, &lsb);
        return nf == 3 ? fmt == GL_RGB : (fmt == GL_RGBA || fmt == GL_BGRA);
    }
    return 1;
}
static GLenum generic_ifmt(GLenum fmt) {
    switch (fmt) { case GL_ALPHA: return GL_ALPHA; case GL_LUMINANCE: return GL_LUMINANCE; case GL_LUMINANCE_ALPHA: return GL_LUMINANCE_ALPHA;
    case GL_RGB: case GL_BGR: case GL_RED: case GL_GREEN: case GL_BLUE: return GL_RGB; }
    return GL_RGBA;
}
// random but valid source data for (fmt, type); floats kept mostly in [0,1] with some outside to test clamping
static void fill_random(uint8_t *buf, size_t n, GLenum type, rng_t *r) {
    if (type == GL_FLOAT) { for (size_t i = 0; i + 4 <= n; i += 4) { float v = rng_int(r, 8) ? rng_f(r, 0, 1) : rng_f(r, -0.5f, 1.5f); memcpy(buf + i, &v, 4); } return; }
    if (type == GL_HALF_FLOAT_ARB) { for (size_t i = 0; i + 2 <= n; i += 2) { uint16_t v = f_to_half(rng_f(r, 0, 1)); memcpy(buf + i, &v, 2); } return; }
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)rng_u32(r);
}

static const char *ename(const en_t *a, int n, GLenum e) { for (int i = 0; i < n; i++) if (a[i].e == e) return a[i].n; return "?"; }
static const char *tname(GLenum t) {
    const char *s = ename(UTYPES, N(UTYPES), t);
    if (*s == '?') s = ename(PTYPES3, N(PTYPES3), t);
    if (*s == '?') s = ename(PTYPES4, N(PTYPES4), t);
    return s;
}
static int ifmt_tol(GLenum ifmt) { for (int i = 0; i < N(IFMTS); i++) if (IFMTS[i].e == ifmt) return IFMTS[i].tol; return 2; }

static void tex_defaults(void) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
static void unpack(int align, int rowlen, int skippix, int skiprows) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, align); glPixelStorei(GL_UNPACK_ROW_LENGTH, rowlen);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, skippix); glPixelStorei(GL_UNPACK_SKIP_ROWS, skiprows);
}
static void draw_tex(int tw, int th, int tol) {
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1, 1, 1, 1);
    glDisable(GL_BLEND);
    gt_quad(8, 8, 2 * tw, 2 * th);
    glDisable(GL_TEXTURE_2D);
}

// a = ifmt, b = fmt, c = type, d = variant: 0 plain 16x16, 1 NPOT 13x7 align 1, 2 unpack row/skip state, 3 null + 2 sub-images
static void t_upload(gt_test *t) {
    GLenum ifmt = t->a, fmt = t->b, type = t->c; int v = t->d;
    int tw = v == 1 ? 13 : 16, th = v == 1 ? 7 : 16;
    int align = v == 1 ? 1 : v == 2 ? 8 : 4, rowlen = v == 2 ? 21 : 0, sp = v == 2 ? 3 : 0, sr = v == 2 ? 2 : 0;
    rng_t r; rng_seed(&r, (uint64_t)ifmt * 7919 + fmt * 104729 + type * 31 + v);
    size_t stride, sz = gt_image_size(tw, th, fmt, type, align, rowlen, sp, sr, &stride);
    uint8_t *buf = malloc(sz + 64); fill_random(buf, sz + 64, type, &r);
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); tex_defaults();
    unpack(align, rowlen, sp, sr);
    if (v == 3) {
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt, tw, th, 0, fmt, type, NULL);
        gt_errcheck(t, "teximage-null");
        // lower-left 16x9 then upper-right 9x7 at (7,9): covers everything, overlapping nothing
        unpack(4, 16, 0, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 9, fmt, type, buf);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 9); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 7);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 7, 9, 9, 7, fmt, type, buf);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 9);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 9, 7, 7, fmt, type, buf);
        gt_errcheck(t, "subimage");
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt, tw, th, 0, fmt, type, buf);
        gt_errcheck(t, "teximage");
    }
    unpack(4, 0, 0, 0);
    int tol = ifmt_tol(ifmt);
    int tb = gt_type_bits(type); if (tb < 8) { int q = 256 / (1 << tb) + 2; if (q > tol) tol = q; }
    if (type == GL_HALF_FLOAT_ARB && tol < 3) tol = 3;
    draw_tex(tw, th, tol);
    gt_img(t, 8, 8, 2 * tw, 2 * th, tol, 0);
    glDeleteTextures(1, &tex); free(buf);
}

// glReadPixels: a = fmt, b = type, c = variant (0 plain, 1 align 1 odd rect, 2 pack row/skip state)
static void draw_random_rgba8(int seed, int w, int h) {
    rng_t r; rng_seed(&r, seed);
    uint8_t *px = malloc(w * h * 4); for (int i = 0; i < w * h * 4; i++) px[i] = (uint8_t)rng_u32(&r);
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); tex_defaults();
    unpack(4, 0, 0, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    gt_quad(0, 0, w, h);
    glDisable(GL_TEXTURE_2D); glDeleteTextures(1, &tex); free(px);
}
static void t_readpixels(gt_test *t) {
    GLenum fmt = t->a, type = t->b; int v = t->c;
    int w = v == 1 ? 13 : 16, h = v == 1 ? 7 : 16, align = v == 1 ? 1 : v == 2 ? 8 : 4;
    int rowlen = v == 2 ? 23 : 0, sp = v == 2 ? 5 : 0, sr = v == 2 ? 3 : 0;
    draw_random_rgba8(4242, 32, 32);
    gt_img(t, 0, 0, 32, 32, 0, 0);   // the source picture itself must match exactly
    size_t stride, sz = gt_image_size(w, h, fmt, type, align, rowlen, sp, sr, &stride);
    uint8_t *buf = malloc(sz + 256); memset(buf, 0xCD, sz + 256);
    glPixelStorei(GL_PACK_ALIGNMENT, align); glPixelStorei(GL_PACK_ROW_LENGTH, rowlen);
    glPixelStorei(GL_PACK_SKIP_PIXELS, sp); glPixelStorei(GL_PACK_SKIP_ROWS, sr);
    glReadPixels(3, 2, w, h, fmt, type, buf);
    gt_errcheck(t, "readpixels");
    glPixelStorei(GL_PACK_ALIGNMENT, 4); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0); glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    int pb = gt_pixel_bytes(fmt, type), n = 0;
    float *vals = malloc(sizeof(float) * w * h * 4);
    char *written = calloc(sz + 256, 1);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        size_t off = (size_t)(sr + y) * stride + (size_t)(sp + x) * pb;
        n += gt_decode(fmt, type, buf + off, vals + n);
        memset(written + off, 1, pb);
    }
    int tb = gt_type_bits(type); double tol = tb >= 8 ? 1.5 / 255 : 1.01 / ((1 << tb) - 1);
    if (fmt == GL_LUMINANCE || fmt == GL_LUMINANCE_ALPHA) tol += 1.5 / 255;
    gt_vals(t, "pixels", tol, vals, n);
    float clobbered = 0;
    for (size_t i = 0; i < sz + 256; i++) if (!written[i] && buf[i] != 0xCD) clobbered++;
    gt_vals(t, "bytes-outside-image-written", 0, &clobbered, 1);
    free(vals); free(buf); free(written);
}

// glGetTexImage: a = internal format of an uploaded random RGBA8 picture, b = fmt, c = type
static void t_gettex(gt_test *t) {
    GLenum ifmt = t->a, fmt = t->b, type = t->c;
    rng_t r; rng_seed(&r, 99 + ifmt);
    uint8_t px[16 * 16 * 4]; for (int i = 0; i < (int)sizeof px; i++) px[i] = (uint8_t)rng_u32(&r);
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); tex_defaults(); unpack(4, 0, 0, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    size_t stride, sz = gt_image_size(16, 16, fmt, type, 4, 0, 0, 0, &stride);
    uint8_t *buf = malloc(sz + 256); memset(buf, 0xCD, sz + 256);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glGetTexImage(GL_TEXTURE_2D, 0, fmt, type, buf);
    gt_errcheck(t, "gettexImage");
    int pb = gt_pixel_bytes(fmt, type), n = 0;
    float *vals = malloc(sizeof(float) * 16 * 16 * 4);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) n += gt_decode(fmt, type, buf + y * stride + x * pb, vals + n);
    int tb = gt_type_bits(type); double tol = tb >= 8 ? 1.5 / 255 : 1.01 / ((1 << tb) - 1);
    tol += ifmt_tol(ifmt) / 255.0;
    gt_vals(t, "texels", tol, vals, n);
    float clobbered = 0; for (size_t i = sz; i < sz + 256; i++) if (buf[i] != 0xCD) clobbered++;
    gt_vals(t, "bytes-after-image-written", 0, &clobbered, 1);
    free(vals); free(buf); glDeleteTextures(1, &tex);
}

// S3TC: a = format, b = 0 full upload, 1 full + compressed sub-image, 2 NPOT 12x20
static void t_s3tc(gt_test *t) {
    GLenum f = t->a; int bs = f == GL_COMPRESSED_RGB_S3TC_DXT1_EXT || f == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ? 8 : 16;
    int w = t->b == 2 ? 12 : 16, h = t->b == 2 ? 20 : 16;
    rng_t r; rng_seed(&r, f + t->b);
    int nb = (w / 4) * (h / 4); uint8_t *d = malloc(nb * bs);
    for (int i = 0; i < nb * bs; i++) d[i] = (uint8_t)rng_u32(&r);
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); tex_defaults(); unpack(4, 0, 0, 0);
    p_glCompressedTexImage2D(GL_TEXTURE_2D, 0, f, w, h, 0, nb * bs, d);
    gt_errcheck(t, "compressed");
    if (t->b == 1) {
        for (int i = 0; i < 4 * bs; i++) d[i] = (uint8_t)rng_u32(&r);
        p_glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 4, 8, 8, 8, f, 4 * bs, d);
        gt_errcheck(t, "compressed-sub");
    }
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ZERO);
    draw_tex(w, h, 0);
    gt_img(t, 8, 8, 2 * w, 2 * h, 12, 0);   // gl4es decodes S3TC to 16-bit colour on the CPU
    glDeleteTextures(1, &tex); free(d);
}

// texture parameters: a = wrap mode, b = min filter, c = NPOT (1) or POT (0), d = mip mode (0 none, 1 GENERATE_MIPMAP, 2 glGenerateMipmapEXT, 3 manual levels)
static void t_texparam(gt_test *t) {
    GLenum wrap = t->a, minf = t->b; int npot = t->c, mip = t->d;
    int w = npot ? 24 : 32, h = npot ? 20 : 16;
    rng_t r; rng_seed(&r, 7 + w);
    uint8_t *px = malloc(w * h * 4);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {   // smooth-ish picture so filtering differences stay small
        uint8_t *p = px + 4 * (y * w + x); p[0] = x * 255 / w; p[1] = y * 255 / h; p[2] = (x ^ y) & 1 ? 200 : 40; p[3] = 128 + (x * 4 & 127);
    }
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); unpack(4, 0, 0, 0);
    if (mip == 1) glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    if (mip == 2) p_glGenerateMipmapEXT(GL_TEXTURE_2D);
    if (mip == 3) {   // distinct solid colour per level: shows which level is sampled
        int lw = w, lh = h, l = 0;
        while (lw > 1 || lh > 1) {
            lw = lw > 1 ? lw / 2 : 1; lh = lh > 1 ? lh / 2 : 1; l++;
            for (int i = 0; i < lw * lh; i++) { px[4 * i] = (uint8_t)(l * 40); px[4 * i + 1] = (uint8_t)(255 - l * 30); px[4 * i + 2] = (uint8_t)(l * 17); px[4 * i + 3] = 255; }
            glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        }
    }
    gt_errcheck(t, "upload");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, minf == GL_NEAREST ? GL_NEAREST : GL_LINEAR);
    float border[4] = { 1, 0, 1, 0.5f }; glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    gt_errcheck(t, "params");
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    // magnified with texcoords outside [0,1] (wrap), then minified at several sizes (mip selection)
    glBegin(GL_QUADS);
    glTexCoord2f(-1.25f, -0.5f); glVertex2f(0, 0); glTexCoord2f(2.25f, -0.5f); glVertex2f(128, 0);
    glTexCoord2f(2.25f, 1.75f); glVertex2f(128, 96); glTexCoord2f(-1.25f, 1.75f); glVertex2f(0, 96);
    glEnd();
    int x = 0;
    for (int s = 32; s >= 2; s /= 2) { gt_quad(x, 100, s, s * h / w > 0 ? s * h / w : 1); x += s + 4; }
    glDisable(GL_TEXTURE_2D);
    gt_img(t, 0, 0, 160, 140, 6, 0.01);
    glDeleteTextures(1, &tex); free(px);
}

// glCopyTexImage2D / glCopyTexSubImage2D from the back buffer into textures of several formats (Unity's grab pass)
static void t_copytex(gt_test *t) {
    GLenum ifmt = t->a; int sub = t->b;
    draw_random_rgba8(555, 32, 32);
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); tex_defaults();
    if (sub) {
        GLenum fmt = ifmt == GL_BGRA ? GL_BGRA : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt == GL_BGRA ? GL_RGBA : ifmt, 32, 32, 0, fmt, GL_UNSIGNED_BYTE, NULL);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 4, 2, 1, 3, 20, 24);
    } else glCopyTexImage2D(GL_TEXTURE_2D, 0, ifmt, 2, 1, 24, 28, 0);
    gt_errcheck(t, "copy");
    glClear(GL_COLOR_BUFFER_BIT);
    draw_tex(sub ? 32 : 24, sub ? 32 : 28, 0);
    gt_img(t, 8, 8, 64, 64, ifmt_tol(ifmt), 0);
    glDeleteTextures(1, &tex);
}

static void reg_name(char *b, size_t n, const char *grp, const char *i, const char *f, const char *ty, int v) {
    snprintf(b, n, "%s/%s/%s/%s/v%d", grp, i, f, ty, v);
}

void reg_pixels(void) {
    char nm[160];
    // every valid (format, type) with the matching generic internal format, GL_RGBA8, all 4 upload variants
    for (int f = 0; f < N(FMTS); f++) {
        en_t types[N(UTYPES) + N(PTYPES3) + N(PTYPES4)]; int nt = 0;
        for (int i = 0; i < N(UTYPES); i++) types[nt++] = UTYPES[i];
        for (int i = 0; i < N(PTYPES3); i++) types[nt++] = PTYPES3[i];
        for (int i = 0; i < N(PTYPES4); i++) types[nt++] = PTYPES4[i];
        for (int ty = 0; ty < nt; ty++) {
            if (!valid_combo(FMTS[f].e, types[ty].e)) continue;
            GLenum g = generic_ifmt(FMTS[f].e);
            for (int v = 0; v < 4; v++) {
                reg_name(nm, sizeof nm, "tex_upload", g == GL_RGBA ? "RGBA" : g == GL_RGB ? "RGB" : FMTS[f].n, FMTS[f].n, types[ty].n, v);
                gt_add(nm, t_upload, g, FMTS[f].e, types[ty].e, v, NULL);
            }
            reg_name(nm, sizeof nm, "tex_upload", "RGBA8", FMTS[f].n, types[ty].n, 0);
            gt_add(nm, t_upload, GL_RGBA8, FMTS[f].e, types[ty].e, 0, NULL);
            // readback with every (format, type) and three pack states
            for (int v = 0; v < 3; v++) {
                snprintf(nm, sizeof nm, "readpixels/%s/%s/v%d", FMTS[f].n, types[ty].n, v);
                gt_add(nm, t_readpixels, FMTS[f].e, types[ty].e, v, 0, NULL);
            }
            static const GLenum gti[] = { GL_RGBA8, GL_RGB8, GL_ALPHA8, GL_LUMINANCE8, GL_LUMINANCE8_ALPHA8, GL_RGBA4 };
            for (int i = 0; i < N(gti); i++) {
                snprintf(nm, sizeof nm, "gettex/%s/%s/%s", IFMTS[0].n, FMTS[f].n, types[ty].n);
                const char *in = "?"; for (int k = 0; k < N(IFMTS); k++) if (IFMTS[k].e == gti[i]) in = IFMTS[k].n;
                snprintf(nm, sizeof nm, "gettex/%s/%s/%s", in, FMTS[f].n, types[ty].n);
                gt_add(nm, t_gettex, gti[i], FMTS[f].e, types[ty].e, 0, NULL);
            }
        }
    }
    // every internal format with a set of common sources
    static const GLenum src[][2] = { { GL_RGBA, GL_UNSIGNED_BYTE }, { GL_BGRA, GL_UNSIGNED_BYTE }, { GL_RGB, GL_UNSIGNED_BYTE },
        { GL_LUMINANCE, GL_UNSIGNED_BYTE }, { GL_ALPHA, GL_UNSIGNED_BYTE }, { GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE },
        { GL_RGBA, GL_UNSIGNED_SHORT }, { GL_ALPHA, GL_UNSIGNED_SHORT }, { GL_LUMINANCE, GL_UNSIGNED_SHORT },
        { GL_RGBA, GL_FLOAT }, { GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4 }, { GL_RGB, GL_UNSIGNED_SHORT_5_6_5 },
        { GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1 }, { GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV } };
    for (int i = 0; i < N(IFMTS); i++) for (int s = 0; s < N(src); s++) {
        reg_name(nm, sizeof nm, "tex_ifmt", IFMTS[i].n, ename(FMTS, N(FMTS), src[s][0]), tname(src[s][1]), 0);
        gt_add(nm, t_upload, IFMTS[i].e, src[s][0], src[s][1], 0, NULL);
        reg_name(nm, sizeof nm, "tex_ifmt", IFMTS[i].n, ename(FMTS, N(FMTS), src[s][0]), tname(src[s][1]), 3);
        gt_add(nm, t_upload, IFMTS[i].e, src[s][0], src[s][1], 3, NULL);
    }
    static const en_t S3[] = { E(COMPRESSED_RGB_S3TC_DXT1_EXT), E(COMPRESSED_RGBA_S3TC_DXT1_EXT), E(COMPRESSED_RGBA_S3TC_DXT3_EXT), E(COMPRESSED_RGBA_S3TC_DXT5_EXT) };
    for (int i = 0; i < N(S3); i++) for (int v = 0; v < 3; v++) { snprintf(nm, sizeof nm, "s3tc/%s/v%d", S3[i].n, v); gt_add(nm, t_s3tc, S3[i].e, v, 0, 0, NULL); }
    static const en_t WR[] = { E(REPEAT), E(CLAMP), E(CLAMP_TO_EDGE), E(CLAMP_TO_BORDER), E(MIRRORED_REPEAT) };
    static const en_t MF[] = { E(NEAREST), E(LINEAR), E(NEAREST_MIPMAP_NEAREST), E(LINEAR_MIPMAP_NEAREST), E(NEAREST_MIPMAP_LINEAR), E(LINEAR_MIPMAP_LINEAR) };
    for (int w = 0; w < N(WR); w++) for (int m = 0; m < N(MF); m++) for (int np = 0; np < 2; np++) for (int mm = 0; mm < 4; mm++) {
        if (m < 2 && mm) continue;
        snprintf(nm, sizeof nm, "texparam/%s/%s/%s/mip%d", WR[w].n, MF[m].n, np ? "npot" : "pot", mm);
        gt_add(nm, t_texparam, WR[w].e, MF[m].e, np, mm, NULL);
    }
    static const if_t CI[] = { I(RGBA, 2), I(RGB, 2), I(RGBA8, 2), I(RGB8, 2), I(ALPHA, 2), I(LUMINANCE, 2), I(LUMINANCE_ALPHA, 2), I(RGB5, 9), I(RGBA4, 18), { GL_BGRA, "BGRA", 2 } };
    for (int i = 0; i < N(CI); i++) for (int s = 0; s < 2; s++) {
        if (CI[i].e == GL_BGRA && !s) continue;
        snprintf(nm, sizeof nm, "copytex/%s/%s", CI[i].n, s ? "sub" : "full"); gt_add(nm, t_copytex, CI[i].e, s, 0, 0, NULL);
    }
}
