/*
 * glxsdl: the GLX host for Westonpack's pass-through gl4es (gl4es_glxpass), without crusty, Weston or Xwayland.
 *
 * That gl4es build exports the glX* entry points the game calls and forwards each to a crusty_glX* symbol that
 * it expects the process to provide; crusty implemented them on an SDL2 window and, with CRUSTY_GL4ES=1, told
 * gl4es to initialise on that context. This library provides the same crusty_glX* symbols on the system SDL2
 * (dlopen'd, so it builds without SDL headers): one fullscreen window, one GLES 2 context, gl4es initialised on
 * it through set_getprocaddress() + initialize_gl4es(), gl4es_pre_swap()/gl4es_post_swap() around every
 * SDL_GL_SwapWindow. SDL2 picks the platform: Knulli's "mali" fbdev driver, ROCKNIX's wayland (Sway).
 * Env: GLXSDL_WIDTH/HEIGHT (default DISPLAY_WIDTH/HEIGHT, else the display), GLXSDL_VSYNC (default 1),
 * GLXSDL_LOG=1. Preload order: gl4es first, this library second (its undefined crusty_glX* bind to us).
 * The fatal-signal handlers in place before SDL starts (box64's: SIGSEGV drives its dynarec write protection) are
 * restored after the window exists: dArkOS's SDL2 installs its own console-restore handlers over them.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>

/* ---- the bits of SDL2 we use, by hand ---- */
typedef struct SDL_Window SDL_Window;
typedef void *SDL_GLContext;
enum { SDL_INIT_VIDEO = 0x20, SDL_INIT_EVENTS = 0x4000 };
enum { SDL_WINDOW_FULLSCREEN = 1, SDL_WINDOW_OPENGL = 2, SDL_WINDOW_SHOWN = 4, SDL_WINDOW_FULLSCREEN_DESKTOP = 0x1001, SDL_WINDOW_ALLOW_HIGHDPI = 0x2000 };
enum { SDL_GL_RED_SIZE, SDL_GL_GREEN_SIZE, SDL_GL_BLUE_SIZE, SDL_GL_ALPHA_SIZE, SDL_GL_BUFFER_SIZE, SDL_GL_DOUBLEBUFFER, SDL_GL_DEPTH_SIZE, SDL_GL_STENCIL_SIZE,
       SDL_GL_ACCUM_RED_SIZE, SDL_GL_ACCUM_GREEN_SIZE, SDL_GL_ACCUM_BLUE_SIZE, SDL_GL_ACCUM_ALPHA_SIZE, SDL_GL_STEREO, SDL_GL_MULTISAMPLEBUFFERS, SDL_GL_MULTISAMPLESAMPLES,
       SDL_GL_ACCELERATED_VISUAL, SDL_GL_RETAINED_BACKING, SDL_GL_CONTEXT_MAJOR_VERSION, SDL_GL_CONTEXT_MINOR_VERSION, SDL_GL_CONTEXT_EGL, SDL_GL_CONTEXT_FLAGS,
       SDL_GL_CONTEXT_PROFILE_MASK };
enum { SDL_GL_CONTEXT_PROFILE_ES = 4 };
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { unsigned format; int w, h, refresh_rate; void *driverdata; } SDL_DisplayMode;
#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000

static int (*p_SDL_Init)(unsigned);
static const char *(*p_SDL_GetError)(void);
static const char *(*p_SDL_GetCurrentVideoDriver)(void);
static int (*p_SDL_GetCurrentDisplayMode)(int, SDL_DisplayMode *);
static int (*p_SDL_GL_SetAttribute)(int, int);
static SDL_Window *(*p_SDL_CreateWindow)(const char *, int, int, int, int, unsigned);
static SDL_GLContext (*p_SDL_GL_CreateContext)(SDL_Window *);
static int (*p_SDL_GL_MakeCurrent)(SDL_Window *, SDL_GLContext);
static void (*p_SDL_GL_SwapWindow)(SDL_Window *);
static int (*p_SDL_GL_SetSwapInterval)(int);
static void *(*p_SDL_GL_GetProcAddress)(const char *);
static void (*p_SDL_GL_GetDrawableSize)(SDL_Window *, int *, int *);
static void (*p_SDL_PumpEvents)(void);
static void (*p_SDL_FlushEvents)(unsigned, unsigned);
static int (*p_SDL_ShowCursor)(int);
static int (*p_SDL_SetHint)(const char *, const char *);

/* ---- gl4es control, resolved in the process ---- */
static void (*p_set_getprocaddress)(void *(*)(const char *));
static void (*p_initialize_gl4es)(void);
static void *(*p_gl4es_GetProcAddress)(const char *);
static void (*p_gl4es_pre_swap)(void);
static void (*p_gl4es_post_swap)(void);

static SDL_Window *win;
static SDL_GLContext ctx;
static int win_w, win_h, ready, failed, logon = -1;
static Display *cur_dpy;
static GLXDrawable cur_draw;
static GLXContext cur_ctx;
static int fbconfig_slot = 1;                       /* the one config; its address is the GLXFBConfig handle */
static void *egl_lib, *gles_lib;

static void LOG(const char *fmt, ...)
{
    if (logon < 0) logon = getenv("GLXSDL_LOG") ? 1 : 0;
    if (!logon) return;
    va_list ap; va_start(ap, fmt); fputs("[glxsdl] ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap);
}

static void *sdl_sym(void *lib, const char *n) { void *p = dlsym(lib, n); if (!p) { fprintf(stderr, "[glxsdl] SDL2 lacks %s\n", n); failed = 1; } return p; }

static void *getproc(const char *name)
{
    void *p = p_SDL_GL_GetProcAddress ? p_SDL_GL_GetProcAddress(name) : NULL;
    if (!p && gles_lib) p = dlsym(gles_lib, name);
    if (!p && egl_lib) p = dlsym(egl_lib, name);
    return p;
}

static int init(void)
{
    if (ready) return 1;
    if (failed) return 0;
    static const int fatal[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT };
    struct sigaction saved[6];
    for (int i = 0; i < 6; i++) sigaction(fatal[i], NULL, &saved[i]);
    const char *names[] = { "libSDL2-2.0.so.0", "libSDL2.so", "libSDL2-2.0.so", NULL };
    void *sdl = NULL;
    for (int i = 0; names[i] && !sdl; i++) sdl = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
    if (!sdl) { fprintf(stderr, "[glxsdl] no SDL2: %s\n", dlerror()); failed = 1; return 0; }
    p_SDL_Init = sdl_sym(sdl, "SDL_Init"); p_SDL_GetError = sdl_sym(sdl, "SDL_GetError");
    p_SDL_GetCurrentVideoDriver = sdl_sym(sdl, "SDL_GetCurrentVideoDriver"); p_SDL_GetCurrentDisplayMode = sdl_sym(sdl, "SDL_GetCurrentDisplayMode");
    p_SDL_GL_SetAttribute = sdl_sym(sdl, "SDL_GL_SetAttribute"); p_SDL_CreateWindow = sdl_sym(sdl, "SDL_CreateWindow");
    p_SDL_GL_CreateContext = sdl_sym(sdl, "SDL_GL_CreateContext"); p_SDL_GL_MakeCurrent = sdl_sym(sdl, "SDL_GL_MakeCurrent");
    p_SDL_GL_SwapWindow = sdl_sym(sdl, "SDL_GL_SwapWindow"); p_SDL_GL_SetSwapInterval = sdl_sym(sdl, "SDL_GL_SetSwapInterval");
    p_SDL_GL_GetProcAddress = sdl_sym(sdl, "SDL_GL_GetProcAddress"); p_SDL_GL_GetDrawableSize = sdl_sym(sdl, "SDL_GL_GetDrawableSize");
    p_SDL_PumpEvents = sdl_sym(sdl, "SDL_PumpEvents"); p_SDL_FlushEvents = sdl_sym(sdl, "SDL_FlushEvents");
    p_SDL_ShowCursor = sdl_sym(sdl, "SDL_ShowCursor"); p_SDL_SetHint = sdl_sym(sdl, "SDL_SetHint");
    if (failed) return 0;
    p_set_getprocaddress = dlsym(RTLD_DEFAULT, "set_getprocaddress");
    p_initialize_gl4es = dlsym(RTLD_DEFAULT, "initialize_gl4es");
    p_gl4es_GetProcAddress = dlsym(RTLD_DEFAULT, "gl4es_GetProcAddress");
    p_gl4es_pre_swap = dlsym(RTLD_DEFAULT, "gl4es_pre_swap");
    p_gl4es_post_swap = dlsym(RTLD_DEFAULT, "gl4es_post_swap");
    if (!p_initialize_gl4es || !p_gl4es_GetProcAddress) { fprintf(stderr, "[glxsdl] gl4es (initialize_gl4es / gl4es_GetProcAddress) not in the process\n"); failed = 1; return 0; }

    p_SDL_SetHint("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS", "0");
    if (p_SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { fprintf(stderr, "[glxsdl] SDL_Init: %s\n", p_SDL_GetError()); failed = 1; return 0; }
    p_SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    p_SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); p_SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    p_SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8); p_SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8); p_SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8); p_SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    p_SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24); p_SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8); p_SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const char *e; int w = 0, h = 0;
    if ((e = getenv("GLXSDL_WIDTH")) || (e = getenv("DISPLAY_WIDTH"))) w = atoi(e);
    if ((e = getenv("GLXSDL_HEIGHT")) || (e = getenv("DISPLAY_HEIGHT"))) h = atoi(e);
    SDL_DisplayMode m = {0};
    if (p_SDL_GetCurrentDisplayMode(0, &m) == 0 && m.w > 0 && m.h > 0) { if (w <= 0) w = m.w; if (h <= 0) h = m.h; }
    if (w <= 0 || h <= 0) { w = 640; h = 480; }
    unsigned flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | ((m.w == w && m.h == h) ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN);
    win = p_SDL_CreateWindow("glxsdl", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, flags);
    if (!win) { fprintf(stderr, "[glxsdl] SDL_CreateWindow: %s\n", p_SDL_GetError()); failed = 1; return 0; }
    ctx = p_SDL_GL_CreateContext(win);
    if (!ctx) { fprintf(stderr, "[glxsdl] SDL_GL_CreateContext: %s\n", p_SDL_GetError()); failed = 1; return 0; }
    p_SDL_GL_MakeCurrent(win, ctx);
    e = getenv("GLXSDL_VSYNC"); p_SDL_GL_SetSwapInterval(e ? atoi(e) : 1);
    p_SDL_ShowCursor(0);
    p_SDL_GL_GetDrawableSize(win, &win_w, &win_h);
    for (int i = 0; i < 6; i++) {                     /* SDL2 builds with crash handlers: give box64 its signals back */
        struct sigaction now; sigaction(fatal[i], NULL, &now);
        if (now.sa_sigaction != saved[i].sa_sigaction) { LOG("SDL replaced the handler of signal %d, restored", fatal[i]); sigaction(fatal[i], &saved[i], NULL); }
    }
    egl_lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL); if (!egl_lib) egl_lib = dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
    gles_lib = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL); if (!gles_lib) gles_lib = dlopen("libGLESv2.so", RTLD_NOW | RTLD_GLOBAL);
    if (p_set_getprocaddress) p_set_getprocaddress(getproc);
    p_initialize_gl4es();
    ready = 1;
    fprintf(stderr, "[glxsdl] SDL2 %s driver, window %dx%d (display %dx%d @ %d Hz), GLES 2 context, gl4es initialised\n",
            p_SDL_GetCurrentVideoDriver(), win_w, win_h, m.w, m.h, m.refresh_rate);
    return 1;
}

/* ---- the crusty_glX* surface the pass-through gl4es binds to ---- */
GLXFBConfig *crusty_glXChooseFBConfig(Display *d, int screen, const int *attribs, int *n)
{
    if (!init()) { *n = 0; return NULL; }
    GLXFBConfig *r = malloc(sizeof *r); r[0] = (GLXFBConfig)&fbconfig_slot; *n = 1;
    LOG("glXChooseFBConfig -> 1");
    return r;
}
GLXFBConfig *crusty_glXGetFBConfigs(Display *d, int screen, int *n) { return crusty_glXChooseFBConfig(d, screen, NULL, n); }
int crusty_glXGetFBConfigAttrib(Display *d, GLXFBConfig c, int attr, int *v)
{
    switch (attr) {
    case GLX_FBCONFIG_ID: *v = 1; break;
    case GLX_BUFFER_SIZE: *v = 32; break;
    case GLX_DOUBLEBUFFER: *v = 1; break;
    case GLX_RED_SIZE: case GLX_GREEN_SIZE: case GLX_BLUE_SIZE: case GLX_ALPHA_SIZE: *v = 8; break;
    case GLX_DEPTH_SIZE: *v = 24; break;
    case GLX_STENCIL_SIZE: *v = 8; break;
    case GLX_SAMPLES: case GLX_SAMPLE_BUFFERS: *v = 0; break;
    case GLX_CONFIG_CAVEAT: *v = GLX_NONE; break;
    case GLX_RENDER_TYPE: *v = GLX_RGBA_BIT; break;
    case GLX_DRAWABLE_TYPE: *v = GLX_WINDOW_BIT; break;
    case GLX_X_RENDERABLE: *v = 1; break;
    case GLX_X_VISUAL_TYPE: *v = GLX_TRUE_COLOR; break;
    case GLX_VISUAL_ID: *v = 0x21; break;
    case GLX_LEVEL: case GLX_STEREO: case GLX_AUX_BUFFERS: case GLX_ACCUM_RED_SIZE: case GLX_ACCUM_GREEN_SIZE: case GLX_ACCUM_BLUE_SIZE: case GLX_ACCUM_ALPHA_SIZE: *v = 0; break;
    case GLX_TRANSPARENT_TYPE: *v = GLX_NONE; break;
    case GLX_MAX_PBUFFER_WIDTH: case GLX_MAX_PBUFFER_HEIGHT: *v = 4096; break;
    case GLX_MAX_PBUFFER_PIXELS: *v = 4096 * 4096; break;
    default: *v = 0; return GLX_BAD_ATTRIBUTE;
    }
    return 0;
}
XVisualInfo *crusty_glXGetVisualFromFBConfig(Display *d, GLXFBConfig c)
{
    XVisualInfo tpl; int n = 0; memset(&tpl, 0, sizeof tpl); tpl.screen = 0; tpl.depth = 24;
    XVisualInfo *v = XGetVisualInfo(d, VisualScreenMask | VisualDepthMask, &tpl, &n);
    LOG("glXGetVisualFromFBConfig -> %d", n);
    return n ? v : NULL;
}
XVisualInfo *crusty_glXChooseVisual(Display *d, int screen, int *attribs) { return crusty_glXGetVisualFromFBConfig(d, NULL); }
int crusty_glXGetConfig(Display *d, XVisualInfo *v, int attr, int *value)
{
    switch (attr) {
    case GLX_USE_GL: case GLX_RGBA: case GLX_DOUBLEBUFFER: *value = 1; break;
    case GLX_BUFFER_SIZE: *value = 32; break;
    case GLX_RED_SIZE: case GLX_GREEN_SIZE: case GLX_BLUE_SIZE: case GLX_ALPHA_SIZE: *value = 8; break;
    case GLX_DEPTH_SIZE: *value = 24; break;
    case GLX_STENCIL_SIZE: *value = 8; break;
    default: *value = 0; break;
    }
    return 0;
}
GLXContext crusty_glXCreateNewContext(Display *d, GLXFBConfig c, int type, GLXContext share, Bool direct) { if (!init()) return NULL; LOG("glXCreateNewContext"); return (GLXContext)ctx; }
GLXContext crusty_glXCreateContext(Display *d, XVisualInfo *v, GLXContext share, Bool direct) { if (!init()) return NULL; LOG("glXCreateContext"); return (GLXContext)ctx; }
GLXContext crusty_glXCreateContextAttribsARB(Display *d, GLXFBConfig c, GLXContext share, Bool direct, const int *attribs) { if (!init()) return NULL; LOG("glXCreateContextAttribsARB"); return (GLXContext)ctx; }
void crusty_glXDestroyContext(Display *d, GLXContext c) { LOG("glXDestroyContext (kept)"); }
void crusty_glXCopyContext(Display *d, GLXContext a, GLXContext b, unsigned long mask) { }
Bool crusty_glXMakeCurrent(Display *d, GLXDrawable dr, GLXContext c)
{
    if (!init()) return False;
    cur_dpy = d; cur_draw = dr; cur_ctx = c;          /* one SDL context, always current on this thread */
    if (c) p_SDL_GL_MakeCurrent(win, ctx);
    LOG("glXMakeCurrent(0x%lx, %p)", dr, c);
    return True;
}
Bool crusty_glXMakeContextCurrent(Display *d, GLXDrawable draw, GLXDrawable read, GLXContext c) { return crusty_glXMakeCurrent(d, draw, c); }
GLXContext crusty_glXGetCurrentContext(void) { return cur_ctx; }
GLXDrawable crusty_glXGetCurrentDrawable(void) { return cur_draw; }
GLXDrawable crusty_glXGetCurrentReadDrawable(void) { return cur_draw; }
Display *crusty_glXGetCurrentDisplay(void) { return cur_dpy; }
void crusty_glXSwapBuffers(Display *d, GLXDrawable dr)
{
    if (!ready) return;
    if (p_gl4es_pre_swap) p_gl4es_pre_swap();
    p_SDL_GL_SwapWindow(win);
    if (p_gl4es_post_swap) p_gl4es_post_swap();
    p_SDL_PumpEvents(); p_SDL_FlushEvents(0, 0xFFFF);   /* keep the window server happy; the game reads input elsewhere */
}
Bool crusty_glXQueryVersion(Display *d, int *maj, int *min) { *maj = 1; *min = 4; return True; }
Bool crusty_glXQueryExtension(Display *d, int *err, int *ev) { if (err) *err = 0; if (ev) *ev = 0; return True; }
const char *crusty_glXQueryExtensionsString(Display *d, int screen) { return "GLX_ARB_get_proc_address GLX_ARB_create_context GLX_ARB_create_context_profile GLX_EXT_swap_control GLX_SGI_swap_control GLX_MESA_swap_control GLX_ARB_multisample"; }
const char *crusty_glXQueryServerString(Display *d, int screen, int name) { return name == GLX_VENDOR ? "glxsdl" : name == GLX_VERSION ? "1.4" : crusty_glXQueryExtensionsString(d, screen); }
const char *crusty_glXGetClientString(Display *d, int name) { return crusty_glXQueryServerString(d, 0, name); }
Bool crusty_glXIsDirect(Display *d, GLXContext c) { return True; }
int crusty_glXQueryContext(Display *d, GLXContext c, int attr, int *v) { if (attr == GLX_FBCONFIG_ID) *v = 1; else if (attr == GLX_RENDER_TYPE) *v = GLX_RGBA_TYPE; else *v = 0; return 0; }
void crusty_glXQueryDrawable(Display *d, GLXDrawable dr, int attr, unsigned *v)
{
    if (!ready) { *v = 0; return; }
    p_SDL_GL_GetDrawableSize(win, &win_w, &win_h);
    if (attr == GLX_WIDTH) *v = win_w; else if (attr == GLX_HEIGHT) *v = win_h; else if (attr == GLX_FBCONFIG_ID) *v = 1; else *v = 0;
}
void crusty_glXWaitGL(void) { void (*f)(void) = p_gl4es_GetProcAddress ? p_gl4es_GetProcAddress("glFinish") : NULL; if (f) f(); }
void crusty_glXWaitX(void) { }
/* the game's swap-interval requests are honoured unless GLXSDL_VSYNC forces a value (a Wayland compositor never
   tears, and waiting for its frame callback quantises a 31 ms frame to 33 or 50 ms) */
static void set_interval(int i)
{
    const char *e = getenv("GLXSDL_VSYNC");
    if (e) i = atoi(e);
    if (ready) p_SDL_GL_SetSwapInterval(i);
    LOG("swap interval %d%s", i, e ? " (forced by GLXSDL_VSYNC)" : "");
}
void crusty_glXSwapIntervalEXT(Display *d, GLXDrawable dr, int i) { set_interval(i); }
int crusty_glXSwapIntervalSGI(int i) { set_interval(i); return 0; }
int crusty_glXSwapIntervalMESA(unsigned i) { set_interval((int)i); return 0; }
int crusty_glXGetSwapIntervalMESA(void) { return 1; }
GLXWindow crusty_glXCreateWindow(Display *d, GLXFBConfig c, Window w, const int *a) { return (GLXWindow)w; }
void crusty_glXDestroyWindow(Display *d, GLXWindow w) { }
GLXPixmap crusty_glXCreatePixmap(Display *d, GLXFBConfig c, Pixmap p, const int *a) { return (GLXPixmap)p; }
void crusty_glXDestroyPixmap(Display *d, GLXPixmap p) { }
GLXPixmap crusty_glXCreateGLXPixmap(Display *d, XVisualInfo *v, Pixmap p) { return (GLXPixmap)p; }
void crusty_glXDestroyGLXPixmap(Display *d, GLXPixmap p) { }
GLXPbuffer crusty_glXCreatePbuffer(Display *d, GLXFBConfig c, const int *a) { return 0; }
void crusty_glXDestroyPbuffer(Display *d, GLXPbuffer p) { }
void crusty_glXSelectEvent(Display *d, GLXDrawable dr, unsigned long m) { }
void crusty_glXGetSelectedEvent(Display *d, GLXDrawable dr, unsigned long *m) { *m = 0; }
void crusty_glXUseXFont(Font f, int first, int count, int base) { }
void *crusty_glXGetProcAddressARB(const GLubyte *name)
{
    const char *n = (const char *)name;
    if (!strncmp(n, "glX", 3)) {
        char buf[128]; snprintf(buf, sizeof buf, "crusty_%s", n);
        void *p = dlsym(RTLD_DEFAULT, buf);                  /* our own glX implementation */
        if (!p) p = dlsym(RTLD_DEFAULT, n);                  /* gl4es's pass-through export */
        return p;
    }
    return p_gl4es_GetProcAddress ? p_gl4es_GetProcAddress(n) : NULL;
}
void *crusty_glXGetProcAddress(const GLubyte *name) { return crusty_glXGetProcAddressARB(name); }
