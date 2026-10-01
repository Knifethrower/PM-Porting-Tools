/*
 * unity4shrink: halve (or quarter, -l 2) large DXT1/DXT5 textures in Unity 4 serialized files
 * (version 9), in place.
 *
 * For low-memory handhelds: gl4es decodes every DXT texture into (shared) RAM, so halving the
 * big atlases cuts the game's memory use. Built for Ittle Dew (Unity 4.7.1f1); works on any
 * Unity 4 file whose Texture2D objects have this layout (checked per object, skipped otherwise):
 *   name (int len, bytes, align 4) | width height completeImageSize format (4 x int)
 *   | mipMap isReadable readAllowed pad (4 bytes) | imageCount dimension (2 x int)
 *   | filter aniso mipBias wrap (4 x 4 bytes) | lightmapFormat colorSpace (2 x int)
 *   | image data (int size, bytes) | optional padding
 *
 * A texture is shrunk when: format DXT1 (10) or DXT5 (12), no mipmaps, one image, both sides
 * divisible by 8, the larger side > MINSIDE (default 512), and (DXT1) no 1-bit alpha blocks.
 * Pixels are averaged 2x2 with alpha weighting and re-encoded with stb_dxt. Work is done in
 * strips of two block rows, so memory use stays small even for 4096x4096 textures.
 *
 * File layout (Unity 4, version 9): big-endian header {metadata size, file size, version, data
 * offset, endianness byte}; the object table (int count; count x {int pathID, uint byteStart,
 * uint byteSize, int typeID, short classID, short destroyed}) is found by scanning the metadata
 * for a count whose entries tile the data section (8-byte aligned); objects follow in table order.
 * The output goes to <file>.tmp (fsync) and replaces <file> only when complete.
 *
 * usage: unity4shrink [-m MINSIDE] [-l LEVELS] [-n] [-q] FILE...
 *   -l 2: quarter size in one step (halve twice where the result still exceeds MINSIDE)
 *   -n: report only; -q: no line for files without work
 * exit: 0 ok (also when nothing to do), 1 error in at least one file.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define STB_DXT_IMPLEMENTATION
#define STB_DXT_STATIC
#include "stb_dxt.h"

#define CLASS_TEXTURE2D 28
#define FMT_DXT1 10
#define FMT_DXT5 12

static int minside = 512, dry_run = 0, quiet = 0, levels = 1;

typedef struct { int32_t path_id; uint32_t start, size; int32_t type_id; int16_t class_id, destroyed; } entry_t;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void put_be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void put_le32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/* ---------------------------------------------------------------- DXT decode (to RGBA8) */
static void rgb565(uint16_t c, uint8_t *o)
{
    int r = c >> 11 & 31, g = c >> 5 & 63, b = c & 31;
    o[0] = (r << 3) | (r >> 2); o[1] = (g << 2) | (g >> 4); o[2] = (b << 3) | (b >> 2);
}

/* colour part of a DXT1/DXT5 block into 16 RGBA pixels (row-major 4x4); dxt1 enables 3-colour mode */
static void decode_color(const uint8_t *b, uint8_t *px, int dxt1)
{
    uint16_t c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
    uint8_t pal[4][4];
    rgb565(c0, pal[0]); rgb565(c1, pal[1]);
    pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
    for (int k = 0; k < 3; k++) {
        if (!dxt1 || c0 > c1) {
            pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3;
            pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3;
        } else {
            pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
            pal[3][k] = 0;
        }
    }
    if (dxt1 && c0 <= c1) pal[3][3] = 0;
    uint32_t idx = le32(b + 4);
    for (int i = 0; i < 16; i++) memcpy(px + 4 * i, pal[idx >> (2 * i) & 3], 4);
}

static void decode_alpha(const uint8_t *b, uint8_t *px)
{
    uint8_t a[8] = { b[0], b[1] };
    if (a[0] > a[1]) for (int i = 1; i < 7; i++) a[i + 1] = ((7 - i) * a[0] + i * a[1]) / 7;
    else { for (int i = 1; i < 5; i++) a[i + 1] = ((5 - i) * a[0] + i * a[1]) / 5; a[6] = 0; a[7] = 255; }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++) px[4 * i + 3] = a[bits >> (3 * i) & 7];
}

/* does a DXT1 texture use 1-bit alpha anywhere (3-colour block with index 3)? */
static int dxt1_has_alpha(const uint8_t *d, size_t nblocks)
{
    for (size_t i = 0; i < nblocks; i++, d += 8) {
        uint16_t c0 = d[0] | d[1] << 8, c1 = d[2] | d[3] << 8;
        if (c0 > c1) continue;
        uint32_t idx = le32(d + 4);
        for (int k = 0; k < 16; k++) if ((idx >> (2 * k) & 3) == 3) return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- shrink one texture */
/* src: w x h DXT data; dst: (w/f) x (h/f) DXT data, same format, f = 2 or 4. Works in strips of f
   block rows (4f pixel rows), so memory stays small even for 4096x4096 textures. Each output pixel
   is the average of an f x f square, colour weighted by alpha (transparent pixels don't bleed their
   colour into edges); quarter size in one step avoids a second round of DXT quantisation. */
static void shrink_dxt(const uint8_t *src, uint8_t *dst, int w, int h, int fmt, int f)
{
    int bs = fmt == FMT_DXT5 ? 16 : 8, bw = w / 4, ow = w / f, obw = ow / 4, n = f * f;
    uint8_t *rows = malloc((size_t)w * 4 * f * 4);   /* 4f decoded input rows, RGBA */
    uint8_t *out = malloc((size_t)ow * 4 * 4);       /* 4 output rows, RGBA */
    uint8_t px[64], blk[64];
    for (int oby = 0; oby < h / (4 * f); oby++) {
        for (int r = 0; r < f; r++) {                 /* decode input block rows f*oby .. f*oby+f-1 */
            const uint8_t *row = src + (size_t)(f * oby + r) * bw * bs;
            for (int bx = 0; bx < bw; bx++) {
                const uint8_t *b = row + (size_t)bx * bs;
                if (fmt == FMT_DXT5) { decode_color(b + 8, px, 0); decode_alpha(b, px); }
                else decode_color(b, px, 1);
                for (int y = 0; y < 4; y++)
                    memcpy(rows + ((size_t)(4 * r + y) * w + 4 * bx) * 4, px + 16 * y, 16);
            }
        }
        for (int y = 0; y < 4; y++)                     /* f x f average, alpha-weighted colour */
            for (int x = 0; x < ow; x++) {
                unsigned asum = 0, wsum[3] = { 0, 0, 0 }, sum[3] = { 0, 0, 0 };
                for (int dy = 0; dy < f; dy++)
                    for (int dx = 0; dx < f; dx++) {
                        const uint8_t *p = rows + ((size_t)(f * y + dy) * w + f * x + dx) * 4;
                        asum += p[3];
                        for (int k = 0; k < 3; k++) { wsum[k] += p[k] * p[3]; sum[k] += p[k]; }
                    }
                uint8_t *o = out + ((size_t)y * ow + x) * 4;
                for (int k = 0; k < 3; k++)
                    o[k] = asum ? (wsum[k] + asum / 2) / asum : (sum[k] + n / 2) / n;
                o[3] = (asum + n / 2) / n;
            }
        for (int bx = 0; bx < obw; bx++) {              /* encode one output block row */
            for (int y = 0; y < 4; y++) memcpy(blk + 16 * y, out + ((size_t)y * ow + 4 * bx) * 4, 16);
            stb_compress_dxt_block(dst + ((size_t)oby * obw + bx) * bs, blk, fmt == FMT_DXT5, STB_DXT_HIGHQUAL);
        }
    }
    free(rows); free(out);
}

/* ---------------------------------------------------------------- Texture2D object */
typedef struct {
    int ok;                   /* layout recognised and texture eligible */
    int w, h, fmt, f;         /* f: reduction factor (2 or 4) */
    size_t fields, data_len_off, data_off, data_len, tail;   /* offsets inside the object */
} tex_t;

static tex_t parse_texture(const uint8_t *o, size_t size)
{
    tex_t t = { 0 };
    if (size < 4) return t;
    uint32_t nlen = le32(o);
    size_t p = 4 + (size_t)nlen; p = (p + 3) & ~(size_t)3;
    if (nlen > 1024 || p + 60 > size) return t;
    t.fields = p;
    t.w = (int)le32(o + p); t.h = (int)le32(o + p + 4); t.fmt = (int)le32(o + p + 12);
    uint32_t complete = le32(o + p + 8);
    int mip = o[p + 16], count = (int)le32(o + p + 20);
    t.data_len_off = p + 52;
    t.data_len = le32(o + t.data_len_off);
    t.data_off = t.data_len_off + 4;
    if (t.data_off + t.data_len > size) return t;
    t.tail = size - (t.data_off + t.data_len);
    int bs = t.fmt == FMT_DXT5 ? 16 : t.fmt == FMT_DXT1 ? 8 : 0;
    if (!bs || mip || count != 1 || t.tail > 3) return t;
    if (t.w <= 0 || t.h <= 0) return t;
    if ((size_t)(t.w / 4) * (t.h / 4) * bs != t.data_len || complete != t.data_len) return t;
    /* halve up to `levels` times while the larger side is still > MINSIDE and the result stays a
       multiple of 4 pixels (DXT blocks): the same choice as running the tool `levels` times */
    int k = 0, m = t.w > t.h ? t.w : t.h;
    while (k < levels && (m >> k) > minside && t.w % (8 << k) == 0 && t.h % (8 << k) == 0) k++;
    if (!k) return t;
    t.f = 1 << k;
    if (t.fmt == FMT_DXT1 && dxt1_has_alpha(o + t.data_off, t.data_len / 8)) { t.ok = -1; return t; }
    t.ok = 1;
    return t;
}

/* build the shrunk object; returns malloc'd bytes and sets *out_size */
static uint8_t *shrink_object(const uint8_t *o, const tex_t *t, size_t *out_size)
{
    int bs = t->fmt == FMT_DXT5 ? 16 : 8, nw = t->w / t->f, nh = t->h / t->f;
    size_t nlen = (size_t)(nw / 4) * (nh / 4) * bs;
    size_t tail = t->tail ? ((4 - (t->data_off + nlen) % 4) % 4) : 0;
    size_t size = t->data_off + nlen + tail;
    uint8_t *n = calloc(1, size);
    memcpy(n, o, t->data_off);
    put_le32(n + t->fields, nw); put_le32(n + t->fields + 4, nh);
    put_le32(n + t->fields + 8, (uint32_t)nlen);
    put_le32(n + t->data_len_off, (uint32_t)nlen);
    shrink_dxt(o + t->data_off, n + t->data_off, t->w, t->h, t->fmt, t->f);
    *out_size = size;
    return n;
}

/* ---------------------------------------------------------------- file */
static int find_table(const uint8_t *m, size_t data_off, size_t file_size, size_t *table, uint32_t *count)
{
    size_t data_len = file_size - data_off;
    for (size_t p = 20; p + 24 < data_off; p += 4) {
        int32_t n = (int32_t)le32(m + p);
        if (n <= 0 || p + 4 + 20 * (size_t)n > data_off) continue;
        size_t pos = 0; int ok = 1;
        for (int32_t i = 0; i < n; i++) {
            const uint8_t *e = m + p + 4 + 20 * (size_t)i;
            uint32_t start = le32(e + 4), size = le32(e + 8);
            if (start != (i ? ((pos + 7) & ~(size_t)7) : 0)) { ok = 0; break; }
            pos = (size_t)start + size;
        }
        if (ok && pos == data_len) { *table = p; *count = (uint32_t)n; return 0; }
    }
    return -1;
}

static int write_all(int fd, const void *buf, size_t n)
{
    const uint8_t *b = buf;
    while (n) {
        ssize_t w = write(fd, b, n > (1 << 22) ? (1 << 22) : n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        b += w; n -= (size_t)w;
    }
    return 0;
}

static int process(const char *path)
{
    const char *base = strrchr(path, '/'); base = base ? base + 1 : path;
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "%s: %s\n", path, strerror(errno)); return 1; }
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < 32) { fprintf(stderr, "%s: too small\n", path); close(fd); return 1; }
    size_t fsize = (size_t)st.st_size;
    const uint8_t *m = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m == MAP_FAILED) { fprintf(stderr, "%s: mmap: %s\n", path, strerror(errno)); close(fd); return 1; }
    size_t data_off = be32(m + 12);
    if (be32(m + 8) != 9 || m[16] != 0 || be32(m + 4) != fsize || data_off >= fsize) {
        fprintf(stderr, "%s: not a little-endian Unity 4 (version 9) serialized file\n", path);
        munmap((void *)m, fsize); close(fd); return 1;
    }
    size_t table; uint32_t count;
    if (find_table(m, data_off, fsize, &table, &count)) {
        fprintf(stderr, "%s: object table not found\n", path);
        munmap((void *)m, fsize); close(fd); return 1;
    }

    /* pass 1: decide */
    entry_t *e = calloc(count, sizeof *e);
    tex_t *tx = calloc(count, sizeof *tx);
    int nshrink = 0, nalpha = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = m + table + 4 + 20 * (size_t)i;
        e[i].path_id = (int32_t)le32(r); e[i].start = le32(r + 4); e[i].size = le32(r + 8);
        e[i].type_id = (int32_t)le32(r + 12); e[i].class_id = (int16_t)(r[16] | r[17] << 8); e[i].destroyed = (int16_t)(r[18] | r[19] << 8);
        if (e[i].class_id == CLASS_TEXTURE2D) {
            tx[i] = parse_texture(m + data_off + e[i].start, e[i].size);
            if (tx[i].ok == 1) nshrink++;
            if (tx[i].ok == -1) nalpha++;
        }
    }
    if (!nshrink || dry_run) {
        uint64_t before = 0;
        if (!nshrink && quiet) { free(e); free(tx); munmap((void *)m, fsize); close(fd); return 0; }
        for (uint32_t i = 0; i < count; i++) if (tx[i].ok == 1) before += tx[i].data_len;
        printf("%s: %d textures to shrink (%.1f MB)%s%s\n", base, nshrink, before / 1048576.0,
               nalpha ? ", DXT1 with alpha kept" : "", dry_run ? " [dry run]" : "");
        free(e); free(tx); munmap((void *)m, fsize); close(fd); return 0;
    }

    /* pass 2: write <path>.tmp */
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { fprintf(stderr, "%s: %s\n", tmp, strerror(errno)); free(e); free(tx); munmap((void *)m, fsize); close(fd); return 1; }
    uint8_t *head = malloc(data_off);
    memcpy(head, m, data_off);
    /* the header is rewritten at the end, once all sizes are known */
    if (write_all(out, head, data_off)) goto fail;
    uint64_t pos = 0, before = 0, after = 0;
    static const uint8_t zeros[8];
    for (uint32_t i = 0; i < count; i++) {
        uint64_t aligned = i ? (pos + 7) & ~(uint64_t)7 : 0;
        if (write_all(out, zeros, (size_t)(aligned - pos))) goto fail;
        pos = aligned;
        const uint8_t *obj = m + data_off + e[i].start;
        size_t size = e[i].size;
        uint8_t *nobj = NULL;
        if (tx[i].ok == 1) {
            nobj = shrink_object(obj, &tx[i], &size);
            before += tx[i].data_len;
            after += le32(nobj + tx[i].data_len_off);   /* the new image data length */
        }
        if (write_all(out, nobj ? nobj : obj, size)) { free(nobj); goto fail; }
        free(nobj);
        uint8_t *r = head + table + 4 + 20 * (size_t)i;
        put_le32(r + 4, (uint32_t)pos); put_le32(r + 8, (uint32_t)size);
        pos += size;
    }
    if (data_off + pos > 0xFFFFFFFFu) { fprintf(stderr, "%s: output too large\n", path); goto fail; }
    put_be32(head + 4, (uint32_t)(data_off + pos));
    if (pwrite(out, head, data_off, 0) != (ssize_t)data_off) goto fail;
    if (fsync(out) || close(out)) { out = -1; goto fail; }
    out = -1;
    munmap((void *)m, fsize); close(fd); m = NULL;
    if (rename(tmp, path)) { fprintf(stderr, "%s: rename: %s\n", path, strerror(errno)); unlink(tmp); free(head); free(e); free(tx); return 1; }
    printf("%s: %d textures shrunk, %.1f -> %.1f MB%s\n", base, nshrink, before / 1048576.0, after / 1048576.0,
           nalpha ? " (DXT1 with alpha kept)" : "");
    free(head); free(e); free(tx);
    return 0;
fail:
    fprintf(stderr, "%s: write failed: %s\n", tmp, strerror(errno));
    if (out >= 0) close(out);
    unlink(tmp);
    free(head); free(e); free(tx);
    if (m) { munmap((void *)m, fsize); close(fd); }
    return 1;
}

int main(int argc, char **argv)
{
    int i = 1, rc = 0;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-n")) dry_run = 1;
        else if (!strcmp(argv[i], "-q")) quiet = 1;
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) { levels = atoi(argv[++i]); if (levels < 1 || levels > 2) levels = 1; }
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) minside = atoi(argv[++i]);
        else { fprintf(stderr, "usage: %s [-m MINSIDE] [-l LEVELS] [-n] [-q] FILE...\n", argv[0]); return 2; }
    }
    if (i == argc) { fprintf(stderr, "usage: %s [-m MINSIDE] [-l LEVELS] [-n] [-q] FILE...\n", argv[0]); return 2; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (; i < argc; i++) rc |= process(argv[i]);
    return rc;
}
