/*
 * ASTC textures for images loaded at runtime with sprite_add / background_add (GMS 1.4 runner).
 *
 * The runner keeps three RGBA copies of every such image: the CBitmap32 frames, the Texture's
 * upload buffer and, once drawn, the GL texture. When "<image>.astc" (written by gmastc) sits
 * next to the image and its frames match the decoded pixels (CRC32), each frame's texture is
 * created from ASTC right away and the upload buffer is dropped. The sprite's CBitmap32 frames
 * are freed after its sprite_collision_mask call or at the end of the step that loaded it:
 * collision masks and sprite_merge read them, and games call those in the same Create event.
 * Backgrounds have no masks, their bitmap goes at once. Port of the droidports gmloader version.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
#include "platform.h"
#include "so_util.h"
#include "libyoyo.h"
#include "thunks/khronos/glad.h"

typedef struct {
    char magic[4];      /* "GMA1" */
    uint32_t count;     /* frames */
    uint32_t format;    /* GL internal format */
    uint32_t reserved;
} pack_header;

typedef struct {
    uint32_t width, height, crc, offset, size;
} pack_frame;

/* GMS 1.4.1804 object layouts */
#define SPR_COUNT(s)     (*(int *)((uint8_t *)(s) + 0x18))
#define SPR_NBITMAPS(s)  (*(int *)((uint8_t *)(s) + 0x44))
#define SPR_BITMAPS(s)   (*(uint8_t ***)((uint8_t *)(s) + 0x48))
#define SPR_NTEXTURES(s) (*(int *)((uint8_t *)(s) + 0x4c))
#define SPR_TEXTURES(s)  (*(int **)((uint8_t *)(s) + 0x50))
#define BG_TEXTURE(b)    (*(int *)((uint8_t *)(b) + 0xc))
#define BG_BITMAP(b)     (*(uint8_t **)((uint8_t *)(b) + 0x10))
#define BMP_W(b)         (*(int *)((b) + 0x8))
#define BMP_H(b)         (*(int *)((b) + 0xc))
#define BMP_PIXELS(b)    (*(uint8_t **)((b) + 0x14))
/* Texture: [1] width, [2] height, [4] flags (0x40 = uploaded), [5] GL name, [9]/[10] upload buffer */

static ABI_ATTR void *(*Sprite_Data)(int) = NULL;
static ABI_ATTR void *(*Background_Data)(int) = NULL;
static ABI_ATTR int32_t *(*GR_Texture_Get_Surface)(int) = NULL;
static ABI_ATTR void (*SetTextureNPOTFlags)(int32_t *) = NULL;
static ABI_ATTR void (*InvalidateTextureState2)() = NULL;
static ABI_ATTR void (*MemoryManager_Free)(const void *) = NULL;
static ABI_ATTR int (*SaveFileExists)(const char *) = NULL;
static ABI_ATTR void (*GetSaveFileName)(char *, int, const char *) = NULL;
static ABI_ATTR const char *(*YYGetString)(RValue *, int) = NULL;
static routine_t F_SpriteAdd = NULL;
static routine_t F_BackgroundAdd = NULL;
static routine_t F_SpriteCollisionMask = NULL;
static routine_t F_SpriteMerge = NULL;

typedef struct { int idx; void *sprite; } pending_t;
static pending_t *pending = NULL;
static int n_pending = 0, cap_pending = 0;

static void destroy_bitmap(uint8_t *bmp)
{
    /* CBitmap32's deleting destructor is the second vtable slot */
    typedef ABI_ATTR void (*dtor_t)(void *);
    (*(dtor_t **)bmp)[1](bmp);
}

static void free_sprite_bitmaps(void *spr)
{
    for (int i = 0; i < SPR_NBITMAPS(spr); i++) {
        if (SPR_BITMAPS(spr)[i]) {
            destroy_bitmap(SPR_BITMAPS(spr)[i]);
            SPR_BITMAPS(spr)[i] = NULL;
        }
    }
}

static int bitmaps_freed(void *spr)
{
    return spr && SPR_NBITMAPS(spr) > 0 && SPR_BITMAPS(spr) && SPR_BITMAPS(spr)[0] == NULL;
}

static void *load_pack(const char *name, size_t *size)
{
    char path[1024];
    if (!SaveFileExists(name))
        return NULL;
    GetSaveFileName(path, sizeof(path) - 8, name);
    strcat(path, ".astc");

    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc(*size);
    if (buf && fread(buf, 1, *size, f) != *size) {
        free(buf);
        buf = NULL;
    }
    fclose(f);

    pack_header *h = (pack_header *)buf;
    if (buf && (*size < sizeof(*h) || memcmp(h->magic, "GMA1", 4) != 0 ||
                *size < sizeof(*h) + h->count * sizeof(pack_frame))) {
        warning("spritehack: %s: bad pack\n", path);
        free(buf);
        buf = NULL;
    }
    return buf;
}

static int frame_matches(const pack_frame *f, uint8_t *bmp, int texid, const char *name, int i)
{
    if (!bmp || BMP_W(bmp) != (int)f->width || BMP_H(bmp) != (int)f->height) {
        warning("spritehack: %s: frame %d size differs\n", name, i);
        return 0;
    }
    int32_t *t = GR_Texture_Get_Surface(texid);
    if (!t || t[1] != (int)f->width || t[2] != (int)f->height || t[10] == 0) {
        warning("spritehack: %s: frame %d texture is not %ux%u RGBA\n", name, i, f->width, f->height);
        return 0;
    }
    if (crc32(0, BMP_PIXELS(bmp), f->width * f->height * 4) != f->crc) {
        warning("spritehack: %s: frame %d pixels differ\n", name, i);
        return 0;
    }
    return 1;
}

static int upload(int texid, const pack_header *h, const pack_frame *f)
{
    int32_t *t = GR_Texture_Get_Surface(texid);
    GLuint tex = t[5];

    while (glGetError() != GL_NO_ERROR)
        ;
    if ((int32_t)tex == -1)
        glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, h->format, f->width, f->height, 0, f->size,
                           (const uint8_t *)h + f->offset);
    if (glGetError() != GL_NO_ERROR) {
        if ((int32_t)t[5] == -1)
            glDeleteTextures(1, &tex);
        InvalidateTextureState2();
        return 0;
    }
    t[5] = tex;
    t[4] |= 0x40;
    SetTextureNPOTFlags(t);
    InvalidateTextureState2();
    MemoryManager_Free((void *)t[9]);
    t[9] = t[10] = 0;
    return 1;
}

/* Replace the textures of frames [0, n) when every frame matches the pack. */
static int convert(const char *name, uint8_t **bitmaps, int *texids, int n)
{
    static int has_astc = -1;
    if (has_astc < 0) {
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        has_astc = ext && strstr(ext, "GL_KHR_texture_compression_astc_ldr") != NULL;
        if (!has_astc)
            warning("spritehack: no ASTC support, images stay RGBA\n");
    }
    if (!has_astc)
        return 0;

    size_t size;
    pack_header *h = (pack_header *)load_pack(name, &size);
    if (!h)
        return 0;

    pack_frame *f = (pack_frame *)(h + 1);
    int ok = (int)h->count == n;
    if (!ok)
        warning("spritehack: %s: %d frames, pack has %u\n", name, n, h->count);
    for (int i = 0; ok && i < n; i++)
        ok = f[i].offset + f[i].size <= size && frame_matches(&f[i], bitmaps[i], texids[i], name, i);
    for (int i = 0; ok && i < n; i++) {
        if (!upload(texids[i], h, &f[i])) {
            warning("spritehack: %s: frame %d upload failed\n", name, i);
            ok = 0;
        }
    }
    if (ok)
        warning("spritehack: %s: %d frame(s) ASTC %ux%u\n", name, n, f[0].width, f[0].height);
    free(h);
    return ok;
}

ABI_ATTR static void sprite_add_hook(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    F_SpriteAdd(ret, self, other, argc, args);
    int idx = (int)ret->rvalue.val;
    void *spr = Sprite_Data(idx);
    if (!spr || SPR_COUNT(spr) <= 0 || SPR_NBITMAPS(spr) != SPR_COUNT(spr) ||
        SPR_NTEXTURES(spr) != SPR_COUNT(spr))
        return;

    if (!convert(YYGetString(args, 0), SPR_BITMAPS(spr), SPR_TEXTURES(spr), SPR_COUNT(spr)))
        return;

    if (n_pending == cap_pending) {
        cap_pending = cap_pending ? cap_pending * 2 : 64;
        pending = (pending_t *)realloc(pending, cap_pending * sizeof(*pending));
    }
    pending[n_pending++] = pending_t{idx, spr};
}

ABI_ATTR static void background_add_hook(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    F_BackgroundAdd(ret, self, other, argc, args);
    void *bg = Background_Data((int)ret->rvalue.val);
    if (!bg || !BG_BITMAP(bg))
        return;

    uint8_t *bmp = BG_BITMAP(bg);
    int tex = BG_TEXTURE(bg);
    if (convert(YYGetString(args, 0), &bmp, &tex, 1)) {
        destroy_bitmap(bmp);
        BG_BITMAP(bg) = NULL;
    }
}

ABI_ATTR static void sprite_collision_mask_hook(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    int idx = YYGetInt32(args, 0);
    void *spr = Sprite_Data(idx);
    if (bitmaps_freed(spr)) {
        warning("spritehack: sprite %d: collision mask after its frames were freed, kept old mask\n", idx);
        return;
    }
    F_SpriteCollisionMask(ret, self, other, argc, args);

    for (int i = 0; i < n_pending; i++) {
        if (pending[i].idx == idx && pending[i].sprite == spr) {
            free_sprite_bitmaps(spr);
            pending[i] = pending[--n_pending];
            break;
        }
    }
}

ABI_ATTR static void sprite_merge_hook(RValue *ret, void *self, void *other, int argc, RValue *args)
{
    if (bitmaps_freed(Sprite_Data(YYGetInt32(args, 0))) || bitmaps_freed(Sprite_Data(YYGetInt32(args, 1)))) {
        warning("spritehack: sprite_merge after the frames were freed, skipped\n");
        return;
    }
    F_SpriteMerge(ret, self, other, argc, args);
}

void spritehack_end_step()
{
    for (int i = 0; i < n_pending; i++) {
        if (Sprite_Data(pending[i].idx) == pending[i].sprite)
            free_sprite_bitmaps(pending[i].sprite);
    }
    n_pending = 0;
}

void register_spritehack_functs(so_module *mod)
{
    ENSURE_SYMBOL(mod, YYGetInt32, "_Z10YYGetInt32PK6RValuei");
    ENSURE_SYMBOL(mod, Sprite_Data, "_Z11Sprite_Datai");
    ENSURE_SYMBOL(mod, Background_Data, "_Z15Background_Datai");
    ENSURE_SYMBOL(mod, GR_Texture_Get_Surface, "_Z22GR_Texture_Get_Surfacei");
    ENSURE_SYMBOL(mod, SetTextureNPOTFlags, "_Z20_SetTextureNPOTFlagsP7Texture");
    ENSURE_SYMBOL(mod, InvalidateTextureState2, "_Z23_InvalidateTextureStatev");
    ENSURE_SYMBOL(mod, MemoryManager_Free, "_ZN13MemoryManager4FreeEPKv");
    ENSURE_SYMBOL(mod, SaveFileExists, "_ZN8LoadSave14SaveFileExistsEPKc");
    ENSURE_SYMBOL(mod, GetSaveFileName, "_ZN8LoadSave16_GetSaveFileNameEPciPKc");
    ENSURE_SYMBOL(mod, YYGetString, "_Z11YYGetStringPK6RValuei");
    ENSURE_SYMBOL(mod, F_SpriteAdd, "_Z11F_SpriteAddR6RValueP9CInstanceS2_iPS_");
    ENSURE_SYMBOL(mod, F_BackgroundAdd, "_Z15F_BackgroundAddR6RValueP9CInstanceS2_iPS_");
    ENSURE_SYMBOL(mod, F_SpriteCollisionMask, "_Z21F_SpriteCollisionMaskR6RValueP9CInstanceS2_iPS_");
    ENSURE_SYMBOL(mod, F_SpriteMerge, "_Z13F_SpriteMergeR6RValueP9CInstanceS2_iPS_");

    Function_Add("sprite_add", sprite_add_hook, 6, 1);
    Function_Add("background_add", background_add_hook, 3, 1);
    Function_Add("sprite_collision_mask", sprite_collision_mask_hook, 9, 1);
    Function_Add("sprite_merge", sprite_merge_hook, 2, 1);
}
