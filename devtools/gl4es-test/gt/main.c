// gt runner: one X window, a fresh GLX context per test, results as text + raw RGBA files in -o DIR.
//   gt -l [-m SUBSTR]              list tests
//   gt -o DIR [-m SUBSTR] [-r FROM TO] [-c CORPUSDIR] [NAME...]
//   gt -p fp|vp SEED               print a generated ARB program
#include "gt.h"
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <stdarg.h>
#include <sys/stat.h>

#define X(t, n) t p_##n;
GT_PROCS
#undef X

static gt_test *tests; static int ntests, captests;
static const char *outdir = ".";

void gt_add(const char *name, gt_fn fn, int a, int b, int c, int d, const void *p) {
    if (ntests == captests) { captests = captests ? captests * 2 : 1024; tests = realloc(tests, captests * sizeof *tests); }
    gt_test *t = &tests[ntests++]; memset(t, 0, sizeof *t);
    snprintf(t->name, sizeof t->name, "%s", name); t->fn = fn; t->a = a; t->b = b; t->c = c; t->d = d; t->p = p;
}

static void fname(const gt_test *t, char *buf, size_t n, const char *suffix, int k) {
    char s[200]; size_t j = 0;
    for (const char *c = t->name; *c && j < sizeof s - 1; c++) s[j++] = (*c == '/' || *c == ' ') ? '_' : *c;
    s[j] = 0;
    if (k >= 0) snprintf(buf, n, "%s/%s.%d%s", outdir, s, k, suffix); else snprintf(buf, n, "%s/%s%s", outdir, s, suffix);
}

void gt_img(gt_test *t, int x, int y, int w, int h, int tol, double maxfrac) {
    uint8_t *px = malloc((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0); glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glFinish();
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    char fn[400]; fname(t, fn, sizeof fn, ".rgba", t->nimg);
    FILE *f = fopen(fn, "wb"); fwrite(px, 4, (size_t)w * h, f); fclose(f);
    const char *base = strrchr(fn, '/') + 1;
    fprintf(t->out, "img %s %d %d %d %g\n", base, w, h, tol, maxfrac);
    t->nimg++; free(px); fflush(t->out);
}

void gt_vals(gt_test *t, const char *key, double tol, const float *v, int n) {
    fprintf(t->out, "val %s %g", key, tol);
    for (int i = 0; i < n; i++) fprintf(t->out, " %.6g", v[i]);
    fprintf(t->out, "\n"); fflush(t->out);
}

void gt_info(gt_test *t, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); fprintf(t->out, "info "); vfprintf(t->out, fmt, ap); fprintf(t->out, "\n"); va_end(ap); fflush(t->out);
}

void gt_skip(gt_test *t, const char *why) { fprintf(t->out, "skip %s\n", why); }

void gt_errcheck(gt_test *t, const char *where) {
    GLenum e; int n = 0;
    while ((e = glGetError()) != GL_NO_ERROR && n++ < 8) gt_info(t, "glerror %s %s", where, gt_enum(e));
}

void gt_pixel_space(void) {
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}

void gt_quad(float x, float y, float w, float h) {
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x, y);
    glTexCoord2f(1, 0); glVertex2f(x + w, y);
    glTexCoord2f(1, 1); glVertex2f(x + w, y + h);
    glTexCoord2f(0, 1); glVertex2f(x, y + h);
    glEnd();
}

const char *gt_enum(GLenum e) {
    static char b[16];
    switch (e) {
    case GL_INVALID_ENUM: return "INVALID_ENUM"; case GL_INVALID_VALUE: return "INVALID_VALUE";
    case GL_INVALID_OPERATION: return "INVALID_OPERATION"; case GL_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case GL_INVALID_FRAMEBUFFER_OPERATION_EXT: return "INVALID_FB_OP";
    }
    snprintf(b, sizeof b, "0x%04x", e); return b;
}

int main(int argc, char **argv) {
    int list = 0, from = 0, to = 1 << 30; const char *match = NULL, *corpus = NULL;
    char **names = NULL; int nnames = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-l")) list = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) outdir = argv[++i];
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) match = argv[++i];
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) corpus = argv[++i];
        else if (!strcmp(argv[i], "-p") && i + 2 < argc) { char *gt_arb_text(const char *, int); fputs(gt_arb_text(argv[i + 1], atoi(argv[i + 2])), stdout); return 0; }
        else if (!strcmp(argv[i], "-r") && i + 2 < argc) { from = atoi(argv[++i]); to = atoi(argv[++i]); }
        else { names = &argv[i]; nnames = argc - i; break; }
    }
    reg_pixels(); reg_arb(); reg_ffp(); reg_state(); reg_fbo(); reg_glsl();
    if (corpus) reg_corpus(corpus);
    if (list) {
        for (int i = 0; i < ntests; i++) if (!match || strstr(tests[i].name, match)) printf("%s\n", tests[i].name);
        return 0;
    }
    mkdir(outdir, 0755);
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "no X display\n"); return 2; }
    int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
                   GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8, None };
    XVisualInfo *vi = glXChooseVisual(dpy, DefaultScreen(dpy), attr);
    if (!vi) { fprintf(stderr, "no visual\n"); return 2; }
    Window win = 1;   // GT_NOWINDOW=1: a GLX layer with its own window (the ports' glxsdl on SDL2) ignores ours
    if (!getenv("GT_NOWINDOW")) {
        XSetWindowAttributes swa = { 0 };
        swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
        win = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 0, 0, W, H, 0, vi->depth, InputOutput, vi->visual,
                            CWColormap, &swa);
        XMapWindow(dpy, win); XSync(dpy, False);
    }
    int idx = -1;
    for (int i = 0; i < ntests; i++) {
        gt_test *t = &tests[i];
        if (match && !strstr(t->name, match)) continue;
        if (nnames) { int ok = 0; for (int k = 0; k < nnames; k++) if (!strcmp(names[k], t->name)) ok = 1; if (!ok) continue; }
        idx++; if (idx < from || idx >= to) continue;
        char fn[400]; fname(t, fn, sizeof fn, ".txt", -1);
        t->out = fopen(fn, "w");
        if (!t->out) { perror(fn); return 2; }
        fprintf(t->out, "test %s\n", t->name); fflush(t->out);
        fprintf(stderr, "### %s\n", t->name); fflush(stderr);
        GLXContext ctx = glXCreateContext(dpy, vi, NULL, True);
        glXMakeCurrent(dpy, win, ctx);
        static int loaded;
        if (!loaded) {
#define X(ty, n) p_##n = (ty)glXGetProcAddressARB((const GLubyte *)#n);
            GT_PROCS
#undef X
            loaded = 1;
        }
        glClearColor(0.25f, 0.5f, 0.75f, 1.0f); glClearDepth(1.0); glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        gt_pixel_space();
        t->fn(t);
        gt_errcheck(t, "end");
        fprintf(t->out, "done\n"); fclose(t->out); t->out = NULL;
        glXMakeCurrent(dpy, None, NULL); glXDestroyContext(dpy, ctx);
        printf("%s\n", t->name); fflush(stdout);
    }
    XDestroyWindow(dpy, win); XCloseDisplay(dpy);
    return 0;
}
