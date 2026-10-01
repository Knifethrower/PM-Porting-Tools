/* xbridge: shows an X display's windows on the device screen through SDL2.
 * Westonpack's headless Weston only puts GL programs on screen (crusty); Wine draws GDI and
 * software DirectDraw with plain X11, which ends up nowhere. xbridge copies the viewable top-level
 * windows of the (rootless) Xwayland in stacking order with XShmGetImage, draws the X cursor
 * (XFixes) and presents the result scaled to the screen with vsync. gptokeyb's virtual keyboard/
 * mouse is grabbed and injected with XTest (see input_open); touch still goes through Weston.
 * XBRIDGE_NOVIDEO=1: input only, no SDL (PC tests).
 * SDL2 is loaded with dlopen (no SDL headers or link-time dependency).
 * Usage: DISPLAY=:0 xbridge            runs until SIGTERM or the X server goes away
 *        XBRIDGE_DUMP=out.ppm xbridge  writes one composited frame and exits (PC tests)
 * Build: see build.sh */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XTest.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* the few SDL2 definitions used (ABI-stable since SDL 2.0.0) */
typedef struct { int x, y, w, h; } SDL_Rect;
typedef struct { uint32_t format; int w, h, refresh_rate; void *driverdata; } SDL_DisplayMode;
typedef union { uint32_t type; uint8_t padding[56]; } SDL_Event;
#define SDL_INIT_VIDEO 0x20u
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x1001u
#define SDL_RENDERER_ACCELERATED 0x2u
#define SDL_RENDERER_PRESENTVSYNC 0x4u
#define SDL_PIXELFORMAT_RGB888 0x16161804u   /* XRGB8888 */
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_BLENDMODE_BLEND 1
#define SDL_QUIT 0x100u

static struct {
    int (*Init)(uint32_t);
    void (*Quit)(void);
    const char *(*GetError)(void);
    int (*GetDesktopDisplayMode)(int, SDL_DisplayMode *);
    void *(*CreateWindow)(const char *, int, int, int, int, uint32_t);
    void *(*CreateRenderer)(void *, int, uint32_t);
    void *(*CreateTexture)(void *, uint32_t, int, int, int);
    void (*DestroyTexture)(void *);
    int (*SetTextureBlendMode)(void *, int);
    int (*UpdateTexture)(void *, const SDL_Rect *, const void *, int);
    int (*SetRenderDrawColor)(void *, uint8_t, uint8_t, uint8_t, uint8_t);
    int (*RenderClear)(void *);
    int (*RenderCopy)(void *, void *, const SDL_Rect *, const SDL_Rect *);
    void (*RenderPresent)(void *);
    int (*PollEvent)(SDL_Event *);
    int (*ShowCursor)(int);
} sdl;

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }
static int ignore_xerror(Display *d, XErrorEvent *e) { (void)d; (void)e; return 0; }  /* windows vanish */
static int xio_gone(Display *d) { (void)d; exit(0); }

static int load_sdl(void)
{
    void *h = dlopen("libSDL2-2.0.so.0", RTLD_NOW);
    if (!h) h = dlopen("libSDL2.so", RTLD_NOW);
    if (!h) { fprintf(stderr, "xbridge: no SDL2: %s\n", dlerror()); return 0; }
#define L(n) if (!(*(void **)&sdl.n = dlsym(h, "SDL_" #n))) { fprintf(stderr, "xbridge: no SDL_%s\n", #n); return 0; }
    L(Init) L(Quit) L(GetError) L(GetDesktopDisplayMode) L(CreateWindow) L(CreateRenderer)
    L(CreateTexture) L(DestroyTexture) L(SetTextureBlendMode) L(UpdateTexture) L(SetRenderDrawColor)
    L(RenderClear) L(RenderCopy) L(RenderPresent) L(PollEvent) L(ShowCursor)
#undef L
    return 1;
}

/* Input: gptokeyb's virtual keyboard/mouse ("Fake Keyboard Mouse") does not get through Weston to
 * Xwayland's clients on the device (touch does). xbridge grabs that device, so Weston no longer sees
 * it, and injects its events with XTest. XBRIDGE_INPUT=<file> reads raw input_events from a file or
 * FIFO instead (PC tests); XBRIDGE_INPUT_NAME changes the device name looked for. */
#define MAX_INPUT 4
static int input_fd[MAX_INPUT], input_n;

static void input_open(void)
{
    const char *file = getenv("XBRIDGE_INPUT");
    const char *want = getenv("XBRIDGE_INPUT_NAME");
    if (!want) want = "Fake Keyboard Mouse";
    if (file) {
        int fd = open(file, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) input_fd[input_n++] = fd;
        return;
    }
    for (int i = 0; i < 32 && input_n < MAX_INPUT; i++) {
        char path[32], name[128] = "";
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        if (strcmp(name, want) == 0 && ioctl(fd, EVIOCGRAB, 1) == 0) {
            fprintf(stderr, "xbridge: input from %s (%s), grabbed\n", path, name);
            input_fd[input_n++] = fd;
        } else {
            close(fd);
        }
    }
}

static void input_pump(Display *dpy)
{
    struct input_event ev[64];
    int dx = 0, dy = 0, sent = 0;
    for (int d = 0; d < input_n; d++) {
        ssize_t r;
        while ((r = read(input_fd[d], ev, sizeof ev)) > 0) {
            for (int i = 0; i < r / (ssize_t)sizeof *ev; i++) {
                struct input_event *e = &ev[i];
                if (e->type == EV_REL) {
                    if (e->code == REL_X) dx += e->value;
                    else if (e->code == REL_Y) dy += e->value;
                    else if (e->code == REL_WHEEL && e->value) {
                        unsigned int b = e->value > 0 ? 4 : 5;
                        XTestFakeButtonEvent(dpy, b, True, 0);
                        XTestFakeButtonEvent(dpy, b, False, 0);
                        sent = 1;
                    }
                } else if (e->type == EV_KEY && e->value != 2) {  /* 2 = autorepeat: X repeats itself */
                    unsigned int b = e->code == BTN_LEFT ? 1 : e->code == BTN_RIGHT ? 3 : e->code == BTN_MIDDLE ? 2 : 0;
                    if (b) XTestFakeButtonEvent(dpy, b, e->value != 0, 0);
                    else if (e->code < 248) XTestFakeKeyEvent(dpy, e->code + 8, e->value != 0, 0);  /* evdev -> X keycode */
                    sent = 1;
                }
            }
        }
    }
    if (dx || dy) { XTestFakeRelativeMotionEvent(dpy, dx, dy, 0); sent = 1; }
    if (sent) XFlush(dpy);
}

/* copy the viewable top-level windows into fb (w x h, XRGB8888), bottom to top */
static void composite(Display *dpy, Window root, XShmSegmentInfo *shm, uint32_t *fb, int w, int h)
{
    Window r, parent, *kids = NULL;
    unsigned int n = 0;
    memset(fb, 0, (size_t)w * h * 4);
    if (!XQueryTree(dpy, root, &r, &parent, &kids, &n)) return;
    for (unsigned int i = 0; i < n; i++) {
        XWindowAttributes a;
        if (!XGetWindowAttributes(dpy, kids[i], &a) || a.map_state != IsViewable || a.class != InputOutput)
            continue;
        int x0 = a.x + a.border_width, y0 = a.y + a.border_width;
        int sx = x0 < 0 ? 0 : x0, sy = y0 < 0 ? 0 : y0;
        int ex = x0 + a.width > w ? w : x0 + a.width, ey = y0 + a.height > h ? h : y0 + a.height;
        if (ex <= sx || ey <= sy) continue;
        XImage *img = XShmCreateImage(dpy, a.visual, a.depth, ZPixmap, NULL, shm, ex - sx, ey - sy);
        if (!img) continue;
        img->data = shm->shmaddr;
        if (img->bits_per_pixel == 32 && XShmGetImage(dpy, kids[i], img, sx - x0, sy - y0, AllPlanes))
            for (int y = 0; y < ey - sy; y++)
                memcpy(fb + (size_t)(sy + y) * w + sx, img->data + (size_t)y * img->bytes_per_line,
                       (size_t)(ex - sx) * 4);
        img->data = NULL;
        XDestroyImage(img);
    }
    if (kids) XFree(kids);
}

static int dump(const char *path, const uint32_t *fb, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        unsigned char px[3] = { fb[i] >> 16, fb[i] >> 8, fb[i] };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    return 0;
}

int main(void)
{
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "xbridge: can't open X display %s\n", getenv("DISPLAY")); return 1; }
    XSetErrorHandler(ignore_xerror);
    XSetIOErrorHandler(xio_gone);
    int scr = DefaultScreen(dpy);
    Window root = RootWindow(dpy, scr);
    int w = DisplayWidth(dpy, scr), h = DisplayHeight(dpy, scr);
    if (!XShmQueryExtension(dpy)) { fprintf(stderr, "xbridge: no MIT-SHM\n"); return 1; }

    XShmSegmentInfo shm = {0};
    shm.shmid = shmget(IPC_PRIVATE, (size_t)w * h * 4, IPC_CREAT | 0600);
    shm.shmaddr = shmat(shm.shmid, NULL, 0);
    shm.readOnly = False;
    if (shm.shmid < 0 || shm.shmaddr == (char *)-1 || !XShmAttach(dpy, &shm)) {
        fprintf(stderr, "xbridge: shared memory failed\n");
        return 1;
    }
    XSync(dpy, False);
    shmctl(shm.shmid, IPC_RMID, NULL);  /* freed when both sides detach */
    uint32_t *fb = malloc((size_t)w * h * 4);

    const char *dumppath = getenv("XBRIDGE_DUMP");
    if (dumppath) {
        composite(dpy, root, &shm, fb, w, h);
        return dump(dumppath, fb, w, h);
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    int xtest_ev, xtest_err, xtest_maj, xtest_min;
    if (!XTestQueryExtension(dpy, &xtest_ev, &xtest_err, &xtest_maj, &xtest_min))
        fprintf(stderr, "xbridge: no XTEST extension, gamepad input stays with Weston\n");
    else
        input_open();
    time_t last_scan = time(NULL);

    if (getenv("XBRIDGE_NOVIDEO")) {
        while (running) {
            input_pump(dpy);
            usleep(5000);
        }
        return 0;
    }

    if (!load_sdl() || sdl.Init(SDL_INIT_VIDEO) < 0) { fprintf(stderr, "xbridge: SDL init failed\n"); return 1; }
    SDL_DisplayMode dm = {0};
    sdl.GetDesktopDisplayMode(0, &dm);
    void *win = sdl.CreateWindow("xbridge", 0, 0, dm.w, dm.h, SDL_WINDOW_FULLSCREEN_DESKTOP);
    void *ren = win ? sdl.CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!ren && win) ren = sdl.CreateRenderer(win, -1, 0);
    void *tex = ren ? sdl.CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, w, h) : NULL;
    if (!tex) { fprintf(stderr, "xbridge: SDL window failed: %s\n", sdl.GetError()); return 1; }
    sdl.ShowCursor(0);
    fprintf(stderr, "xbridge: X screen %dx%d -> display %dx%d\n", w, h, dm.w, dm.h);

    /* X screen scaled to fit, aspect kept */
    SDL_Rect dst = { 0, 0, dm.w, dm.h };
    if ((long)dm.w * h > (long)dm.h * w) { dst.w = dm.h * w / h; dst.x = (dm.w - dst.w) / 2; }
    else { dst.h = dm.w * h / w; dst.y = (dm.h - dst.h) / 2; }

    int xfixes_ev, xfixes_err, have_xfixes = XFixesQueryExtension(dpy, &xfixes_ev, &xfixes_err);
    if (have_xfixes) XFixesSelectCursorInput(dpy, root, XFixesDisplayCursorNotifyMask);
    void *ctex = NULL;
    int cw = 0, ch = 0, chx = 0, chy = 0, cursor_dirty = have_xfixes;

    while (running) {
        SDL_Event ev;
        while (sdl.PollEvent(&ev)) if (ev.type == SDL_QUIT) running = 0;
        input_pump(dpy);
        if (!input_n && xtest_maj && time(NULL) - last_scan >= 2) {  /* gptokeyb may start later */
            last_scan = time(NULL);
            input_open();
        }
        while (XPending(dpy)) {
            XEvent xe;
            XNextEvent(dpy, &xe);
            if (have_xfixes && xe.type == xfixes_ev + XFixesCursorNotify) cursor_dirty = 1;
        }
        if (cursor_dirty) {
            cursor_dirty = 0;
            XFixesCursorImage *ci = XFixesGetCursorImage(dpy);
            if (ci && ci->width && ci->height) {
                uint32_t *px = malloc((size_t)ci->width * ci->height * 4);
                for (int i = 0; i < ci->width * ci->height; i++) px[i] = (uint32_t)ci->pixels[i];
                if (ctex) sdl.DestroyTexture(ctex);
                ctex = sdl.CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ci->width, ci->height);
                if (ctex) {
                    sdl.SetTextureBlendMode(ctex, SDL_BLENDMODE_BLEND);
                    sdl.UpdateTexture(ctex, NULL, px, ci->width * 4);
                }
                cw = ci->width; ch = ci->height; chx = ci->xhot; chy = ci->yhot;
                free(px);
            }
            if (ci) XFree(ci);
        }

        composite(dpy, root, &shm, fb, w, h);
        sdl.UpdateTexture(tex, NULL, fb, w * 4);
        sdl.SetRenderDrawColor(ren, 0, 0, 0, 255);
        sdl.RenderClear(ren);
        sdl.RenderCopy(ren, tex, NULL, &dst);
        if (ctex) {
            Window rr, cr;
            int px, py, wx, wy;
            unsigned int mask;
            if (XQueryPointer(dpy, root, &rr, &cr, &px, &py, &wx, &wy, &mask)) {
                SDL_Rect c = { dst.x + (px - chx) * dst.w / w, dst.y + (py - chy) * dst.h / h,
                               cw * dst.w / w, ch * dst.h / h };
                sdl.RenderCopy(ren, ctex, NULL, &c);
            }
        }
        sdl.RenderPresent(ren);

        {   /* input check for the log: X pointer position and keyboard focus, every 2 s if changed */
            static time_t last;
            static int lx = -1, ly = -1;
            static Window lf;
            time_t now = time(NULL);
            if (now - last >= 2) {
                Window rr, cr, focus;
                int px, py, wx, wy, revert;
                unsigned int mask;
                last = now;
                XQueryPointer(dpy, root, &rr, &cr, &px, &py, &wx, &wy, &mask);
                XGetInputFocus(dpy, &focus, &revert);
                if (px != lx || py != ly || focus != lf)
                    fprintf(stderr, "xbridge: pointer %d,%d buttons 0x%x, focus 0x%lx\n", px, py, mask, focus);
                lx = px; ly = py; lf = focus;
            }
        }
    }
    sdl.Quit();
    XShmDetach(dpy, &shm);
    XCloseDisplay(dpy);
    return 0;
}
