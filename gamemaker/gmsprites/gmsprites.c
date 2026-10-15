/*
 * gmsprites: ASTC packs for the images a GameMaker game loads at runtime with sprite_add /
 * background_add, for the "spritehack" in gmloader-next (gmloader-next-patches/04-spritehack).
 *
 *   gmsprites <list> <block> [quality]
 *
 * For each line "<png> <frames> <removeback> <smooth>" of <list> (paths relative to the current
 * folder, the game's save_dir; made by sprite_list.py), write <png>.astc: the frames the runner
 * makes from <png> (strip cut into <frames>, background removal and edge smoothing as the GMS 1.4
 * runner does them), ASTC encoded, each with the CRC32 of its RGBA pixels so the loader can check
 * that it matches what the runner decoded. <block> is 4x4, 5x5 or 6x6; [quality] is an astcenc
 * preset (default 10 = fast). Finished packs are skipped, so an interrupted run can be restarted.
 * Texture pages inside data.win are gmtoolkit's job (externalize_textures).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <zlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

/* astc_shim.cpp */
const char *astc_setup(int block, float quality, unsigned threads);
const char *astc_compress(unsigned char *rgba, int w, int h, unsigned char *out, size_t len);

static int bx, by;

static void die(const char *fmt, const char *arg)
{
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
    exit(1);
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(*size ? *size : 1);
    if (!buf || fread(buf, 1, *size, f) != *size)
        die("cannot read %s", path);
    fclose(f);
    return buf;
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static size_t astc_size(int w, int h)
{
    return (size_t)((w + bx - 1) / bx) * ((h + by - 1) / by) * 16;
}

static void encode(uint8_t *rgba, int w, int h, uint8_t *out)
{
    const char *err = astc_compress(rgba, w, h, out, astc_size(w, h));
    if (err)
        die("astcenc: %s", err);
}

static void setup(const char *block, const char *preset)
{
    if (sscanf(block, "%dx%d", &bx, &by) != 2 || bx != by || bx < 4 || bx > 6)
        die("block must be 4x4, 5x5 or 6x6, not %s", block);
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    const char *err = astc_setup(bx, preset ? atof(preset) : 10.0f, n < 1 ? 1 : n > 64 ? 64 : n);
    if (err)
        die("astcenc: %s", err);
}

/* Write a file through a temporary name so that a finished file is always complete. */
static void write_atomic(const char *path, const void *a, size_t alen, const void *b, size_t blen)
{
    char tmp[4096 + 16];   /* path: at most 4095 + ".astc" */
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f || fwrite(a, 1, alen, f) != alen || (blen && fwrite(b, 1, blen, f) != blen) || fclose(f))
        die("cannot write %s", tmp);
    if (rename(tmp, path))
        die("cannot rename %s", tmp);
}

/* --- sprites -------------------------------------------------------------------------- */

/* CBitmap32::RemoveBackground + ImproveBoundary: pixels with the bottom-left pixel's colour
 * become transparent, then transparent pixels take the colour of an opaque neighbour. */
static void remove_background(uint32_t *p, int w, int h)
{
    uint32_t bg = p[(size_t)(h - 1) * w] & 0xffffff;
    for (size_t i = 0; i < (size_t)w * h; i++)
        if ((p[i] & 0xffffff) == bg)
            p[i] = bg;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t *q = &p[(size_t)y * w + x], n;
            if (*q & 0xff000000)
                continue;
            if ((x > 0 && ((n = q[-1]) & 0xff000000)) ||
                (x < w - 1 && ((n = q[1]) & 0xff000000)) ||
                (y > 0 && ((n = q[-w]) & 0xff000000)) ||
                (y < h - 1 && ((n = q[w]) & 0xff000000)))
                *q = n & 0xffffff;
        }
    }
}

/* CBitmap32::SmoothEdges: every transparent pixel lowers the alpha of its 3x3 neighbourhood
 * by 32 (where it is at least 32), in place and in scan order. */
static void smooth_edges(uint32_t *p, int w, int h)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (p[(size_t)y * w + x] & 0xff000000)
                continue;
            int y1 = y + 1 < h ? y + 1 : h - 1, x1 = x + 1 < w ? x + 1 : w - 1;
            for (int yy = y > 0 ? y - 1 : 0; yy <= y1; yy++)
                for (int xx = x > 0 ? x - 1 : 0; xx <= x1; xx++)
                    if (p[(size_t)yy * w + xx] > 0x1fffffff)
                        p[(size_t)yy * w + xx] -= 0x20000000;
        }
    }
}

typedef struct { char magic[4]; uint32_t count, format, reserved; } pack_header;
typedef struct { uint32_t width, height, crc, offset, size; } pack_frame;

static uint32_t gl_format(void)
{
    return bx == 4 ? 0x93B0 : bx == 5 ? 0x93B2 : 0x93B4;
}

static void sprite(const char *png, int frames, int removeback, int smooth)
{
    char out[4096 + 8];    /* png: at most 4095 characters (list format) */
    snprintf(out, sizeof(out), "%s.astc", png);
    if (exists(out))
        return;

    size_t size;
    uint8_t *file = read_file(png, &size);
    if (!file) {
        fprintf(stderr, "%s: missing, skipped\n", png);
        return;
    }
    int w, h, n;
    uint32_t *px = (uint32_t *)stbi_load_from_memory(file, size, &w, &h, &n, 4);
    free(file);
    if (!px)
        die("cannot decode %s", png);

    if (removeback) {
        remove_background(px, w, h);
        if (smooth)
            smooth_edges(px, w, h);
    }
    if (frames < 1)
        frames = 1;
    int fw = w / frames;

    size_t fsize = astc_size(fw, h), hdr = sizeof(pack_header) + frames * sizeof(pack_frame);
    uint8_t *data = malloc(fsize * frames), *rgba = malloc((size_t)fw * h * 4);
    pack_header *ph = calloc(1, hdr);
    pack_frame *pf = (pack_frame *)(ph + 1);
    memcpy(ph->magic, "GMA1", 4);
    ph->count = frames;
    ph->format = gl_format();
    for (int i = 0; i < frames; i++) {
        for (int y = 0; y < h; y++)
            memcpy(rgba + (size_t)y * fw * 4, px + (size_t)y * w + (size_t)i * fw, (size_t)fw * 4);
        encode(rgba, fw, h, data + fsize * i);
        pf[i] = (pack_frame){fw, h, crc32(0, rgba, fw * h * 4), hdr + fsize * i, fsize};
    }
    write_atomic(out, ph, hdr, data, fsize * frames);
    stbi_image_free(px);
    free(rgba);
    free(data);
    free(ph);
}

static int run_sprites(const char *list)
{
    FILE *f = fopen(list, "r");
    if (!f)
        die("cannot open %s", list);
    char line[4096], png[4096];
    int frames, removeback, smooth, done = 0, total = 0;
    while (fgets(line, sizeof(line), f))
        total += line[0] != '#' && sscanf(line, "%4095s %d %d %d", png, &frames, &removeback, &smooth) == 4;
    rewind(f);
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || sscanf(line, "%4095s %d %d %d", png, &frames, &removeback, &smooth) != 4)
            continue;
        sprite(png, frames, removeback, smooth);
        printf("images %d/%d\n", ++done, total);
        fflush(stdout);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: gmsprites <list> <block> [quality]\n");
        return 2;
    }
    setup(argv[2], argc > 3 ? argv[3] : NULL);
    return run_sprites(argv[1]);
}
