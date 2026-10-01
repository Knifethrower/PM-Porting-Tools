/* fpslog: TEST ONLY x86_64 preload for the emulated game (box64 BOX64_LD_PRELOAD). Counts glXSwapBuffers
 * calls and prints "[fps] N.N (longest frame N ms, N grabs WxH, N readbacks)" once a second to stderr (Unity 4
 * sends stderr to its -logFile), independent of crusty's CRUSTY_FPS, which produced nothing on the TrimUI
 * Smart Pro. grabs = glCopyTexSubImage2D calls (Unity's GrabPass; WxH = the last one's size), readbacks =
 * glReadPixels calls made by the game itself (gl4es's own fallback readback is internal and not counted).
 * FPSLOG_TEX=1 also prints "[tex] ..." once per distinct glTexImage2D / glTexSubImage2D shape and "[copy] ..."
 * once per distinct glCopyTexSubImage2D (bound framebuffer, size, offsets), to see what a driver mishandles.
 * FPSLOG_FIXBGRA=1 hands gl4es plain RGBA bytes instead of BGRA + GL_UNSIGNED_INT_8_8_8_8 uploads (=2: as RGBA4444).
 * FPSLOG_FINISH0 bits: 1 glFinish / 2 glFlush before a copy from the window surface, 4 glFinish / 8 glFlush after any copy.
 * Build: gcc -O2 -shared -fPIC -o libfpslog.so fpslog.c -ldl      License: 0BSD. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct _XDisplay Display;
typedef unsigned long GLXDrawable;
typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;

static void (*real_swap)(Display *, GLXDrawable);
static void (*real_copy)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
static void (*real_read)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
static void (*real_teximage)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void (*real_texsub)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
static void (*real_getiv)(GLenum, GLint *);
static void (*real_finish)(void);
static void (*real_flush)(void);
static int frames, grabs, reads, grab_w, grab_h, logtex = -1, fixbgra = -1, finish0 = -1;
static double t0, tlast, longest;

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void init_env(void)
{
    if (logtex < 0) logtex = getenv("FPSLOG_TEX") ? 1 : 0;
    if (fixbgra < 0) fixbgra = getenv("FPSLOG_FIXBGRA") ? atoi(getenv("FPSLOG_FIXBGRA")) : 0;
    if (finish0 < 0) finish0 = getenv("FPSLOG_FINISH0") ? atoi(getenv("FPSLOG_FINISH0")) : 0;
}

#define NSHAPES 512
static struct { GLenum target; GLint level, ifmt; GLsizei w, h; GLenum fmt, type; int sub, hasdata; } shapes[NSHAPES];
static int nshapes;

static void note_shape(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLenum fmt, GLenum type, int sub, int hasdata)
{
    if (!logtex) return;
    for (int i = 0; i < nshapes; i++)
        if (shapes[i].target == target && shapes[i].level == level && shapes[i].ifmt == ifmt && shapes[i].w == w && shapes[i].h == h
            && shapes[i].fmt == fmt && shapes[i].type == type && shapes[i].sub == sub && shapes[i].hasdata == hasdata) return;
    if (nshapes < NSHAPES) {
        shapes[nshapes].target = target; shapes[nshapes].level = level; shapes[nshapes].ifmt = ifmt; shapes[nshapes].w = w; shapes[nshapes].h = h;
        shapes[nshapes].fmt = fmt; shapes[nshapes].type = type; shapes[nshapes].sub = sub; shapes[nshapes].hasdata = hasdata; nshapes++;
        fprintf(stderr, "[tex] %s target=0x%x level=%d ifmt=0x%x %dx%d fmt=0x%x type=0x%x data=%d\n",
                sub ? "sub" : "image", target, level, ifmt, w, h, fmt, type, hasdata);
        fflush(stderr);
    }
}

static void *bgra8888_to_rgba(const void *data, GLsizei w, GLsizei h)
{
    const unsigned char *s = data; unsigned char *d = malloc((size_t)w * h * 4);
    if (!d) return NULL;
    for (size_t i = 0; i < (size_t)w * h; i++) {   /* memory per pixel (little endian): A R G B */
        d[i*4+0] = s[i*4+1]; d[i*4+1] = s[i*4+2]; d[i*4+2] = s[i*4+3]; d[i*4+3] = s[i*4+0];
    }
    return d;
}

static void *bgra8888_to_rgba4444(const void *data, GLsizei w, GLsizei h)
{
    const unsigned char *s = data; unsigned short *d = malloc((size_t)w * h * 2);
    if (!d) return NULL;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        unsigned a = s[i*4+0] >> 4, r = s[i*4+1] >> 4, g = s[i*4+2] >> 4, b = s[i*4+3] >> 4;
        d[i] = (unsigned short)((r << 12) | (g << 8) | (b << 4) | a);
    }
    return d;
}

void glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type, const void *data)
{
    if (!real_teximage) real_teximage = dlsym(RTLD_NEXT, "glTexImage2D");
    init_env();
    note_shape(target, level, ifmt, w, h, fmt, type, 0, data != NULL);
    if (fixbgra == 2 && data && fmt == 0x80E1 && type == 0x8035) {
        void *tmp = bgra8888_to_rgba4444(data, w, h);
        if (tmp) { real_teximage(target, level, 0x1908, w, h, border, 0x1908, 0x8033, tmp); free(tmp); return; }
    }
    if (fixbgra && data && fmt == 0x80E1 && type == 0x8035) {
        void *tmp = bgra8888_to_rgba(data, w, h);
        if (tmp) { real_teximage(target, level, ifmt, w, h, border, 0x1908, 0x1401, tmp); free(tmp); return; }
    }
    real_teximage(target, level, ifmt, w, h, border, fmt, type, data);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xo, GLint yo, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const void *data)
{
    if (!real_texsub) real_texsub = dlsym(RTLD_NEXT, "glTexSubImage2D");
    init_env();
    note_shape(target, level, 0, w, h, fmt, type, 1, data != NULL);
    if (fixbgra == 2 && data && fmt == 0x80E1 && type == 0x8035) {
        void *tmp = bgra8888_to_rgba4444(data, w, h);
        if (tmp) { real_texsub(target, level, xo, yo, w, h, 0x1908, 0x8033, tmp); free(tmp); return; }
    }
    if (fixbgra && data && fmt == 0x80E1 && type == 0x8035) {
        void *tmp = bgra8888_to_rgba(data, w, h);
        if (tmp) { real_texsub(target, level, xo, yo, w, h, 0x1908, 0x1401, tmp); free(tmp); return; }
    }
    real_texsub(target, level, xo, yo, w, h, fmt, type, data);
}

static struct { GLint fbo; GLsizei w, h; GLint x, y, xo, yo; } copies[64];
static int ncopies;

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (!real_copy) real_copy = dlsym(RTLD_NEXT, "glCopyTexSubImage2D");
    init_env();
    grabs++; grab_w = width; grab_h = height;
    if (logtex || finish0) {
        if (!real_getiv) real_getiv = dlsym(RTLD_NEXT, "glGetIntegerv");
        GLint fbo = -1; real_getiv(0x8CA6, &fbo);   /* GL_FRAMEBUFFER_BINDING */
        /* bits: 1 finish before window copies, 2 flush before window copies, 4 finish after any copy, 8 flush after any copy */
        if (!real_flush) real_flush = dlsym(RTLD_NEXT, "glFlush");
        if (!real_finish) real_finish = dlsym(RTLD_NEXT, "glFinish");
        if (fbo == 0 && (finish0 & 1) && real_finish) real_finish();
        if (fbo == 0 && (finish0 & 2) && real_flush) real_flush();
        if (finish0 & 12) {
            real_copy(target, level, xoffset, yoffset, x, y, width, height);
            if ((finish0 & 4) && real_finish) real_finish();
            if ((finish0 & 8) && real_flush) real_flush();
            return;
        }
        if (logtex) {
            int seen = 0;
            for (int i = 0; i < ncopies; i++)
                if (copies[i].fbo == fbo && copies[i].w == width && copies[i].h == height && copies[i].x == x && copies[i].y == y && copies[i].xo == xoffset && copies[i].yo == yoffset) { seen = 1; break; }
            if (!seen && ncopies < 64) {
                copies[ncopies].fbo = fbo; copies[ncopies].w = width; copies[ncopies].h = height; copies[ncopies].x = x; copies[ncopies].y = y; copies[ncopies].xo = xoffset; copies[ncopies].yo = yoffset; ncopies++;
                fprintf(stderr, "[copy] fbo=%d %dx%d from (%d,%d) to (%d,%d) level=%d\n", fbo, width, height, x, y, xoffset, yoffset, level);
                fflush(stderr);
            }
        }
    }
    real_copy(target, level, xoffset, yoffset, x, y, width, height);
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *data)
{
    if (!real_read) real_read = dlsym(RTLD_NEXT, "glReadPixels");
    reads++;
    real_read(x, y, width, height, format, type, data);
}

void glXSwapBuffers(Display *dpy, GLXDrawable drawable)
{
    if (!real_swap) real_swap = dlsym(RTLD_NEXT, "glXSwapBuffers");
    real_swap(dpy, drawable);
    double t = now();
    if (tlast && t - tlast > longest) longest = t - tlast;
    tlast = t;
    frames++;
    if (!t0) t0 = t;
    if (t - t0 >= 1.0) {
        fprintf(stderr, "[fps] %.1f (longest frame %.0f ms, %d grabs %dx%d, %d readbacks)\n", frames / (t - t0), longest * 1000.0, grabs, grab_w, grab_h, reads);
        fflush(stderr);
        frames = 0; grabs = 0; reads = 0; t0 = t; longest = 0;
    }
}
