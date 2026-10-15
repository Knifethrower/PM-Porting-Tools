// Snapshot shim for glretrace: a libGL.so.1 that forwards everything to the real library (its DT_NEEDED
// "libsnapreal.so", a symlink next to it) and saves the back buffer every SNAP_EVERY swaps as
// SNAP_DIR/<frame>.ppm (RGB, top row first). glretrace's own snapshots need PBO queries gl4es lacks.
// Pack state is saved and restored, so the replay itself is unchanged.
#include <GL/gl.h>
#include <GL/glx.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static void *real(const char *n) {
    static void *h;
    if (!h) h = dlopen("libsnapreal.so", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("libsnapreal.so", RTLD_NOW);
    return dlsym(h, n);
}
void glXSwapBuffers(Display *d, GLXDrawable w) {
    static int frame, every = -1; static const char *dir;
    static void (*swap)(Display *, GLXDrawable);
    static void (*rp)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
    static void (*gi)(GLenum, GLint *); static void (*ps)(GLenum, GLint); static void (*rb)(GLenum);
    if (every < 0) {
        every = getenv("SNAP_EVERY") ? atoi(getenv("SNAP_EVERY")) : 10; dir = getenv("SNAP_DIR");
        swap = real("glXSwapBuffers"); rp = real("glReadPixels"); gi = real("glGetIntegerv"); ps = real("glPixelStorei"); rb = real("glReadBuffer");
    }
    frame++;
    if (dir && every > 0 && frame % every == 0) {
        unsigned ww = 0, hh = 0;
        void (*qd)(Display *, GLXDrawable, int, unsigned *) = real("glXQueryDrawable");
        if (qd) { qd(d, w, GLX_WIDTH, &ww); qd(d, w, GLX_HEIGHT, &hh); }
        if (!ww || !hh || ww > 8192 || hh > 8192) {
            Window root; int x, y; unsigned bw, dep;
            if (!XGetGeometry(d, w, &root, &x, &y, &ww, &hh, &bw, &dep)) ww = hh = 0;
        }
        if (!ww || !hh || ww > 8192 || hh > 8192) { swap(d, w); return; }
        GLint al, rl, sp, sr, rbuf, fb = 0;
        gi(GL_PACK_ALIGNMENT, &al); gi(GL_PACK_ROW_LENGTH, &rl); gi(GL_PACK_SKIP_PIXELS, &sp); gi(GL_PACK_SKIP_ROWS, &sr);
        gi(GL_READ_BUFFER, &rbuf); gi(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &fb);
        while (((GLenum (*)(void))real("glGetError"))() != GL_NO_ERROR) {}
        ps(GL_PACK_ALIGNMENT, 1); ps(GL_PACK_ROW_LENGTH, 0); ps(GL_PACK_SKIP_PIXELS, 0); ps(GL_PACK_SKIP_ROWS, 0);
        if (!fb) rb(GL_BACK);
        unsigned char *px = malloc((size_t)ww * hh * 3);
        rp(0, 0, ww, hh, GL_RGB, GL_UNSIGNED_BYTE, px);
        char fn[512]; snprintf(fn, sizeof fn, "%s/%06d.ppm", dir, frame);
        FILE *f = fopen(fn, "wb");
        if (f) { fprintf(f, "P6\n%u %u\n255\n", ww, hh); for (int r = (int)hh - 1; r >= 0; r--) fwrite(px + (size_t)r * ww * 3, 3, ww, f); fclose(f); }
        free(px);
        ps(GL_PACK_ALIGNMENT, al); ps(GL_PACK_ROW_LENGTH, rl); ps(GL_PACK_SKIP_PIXELS, sp); ps(GL_PACK_SKIP_ROWS, sr);
        if (!fb) rb(rbuf);
        while (((GLenum (*)(void))real("glGetError"))() != GL_NO_ERROR) {}
    }
    swap(d, w);
}
