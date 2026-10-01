/*
 * xstub: a libX11.so.6 replacement for Unity 4 Linux players under box64 + crusty (Westonpack), so that no X
 * server (Weston + Xwayland, ~40 MB on a 1 GB handheld) has to run. It keeps the real Xlib ABI (struct _XDisplay,
 * Screen, Visual, XEvent, XImage) because box64's libX11 wrapper reads Display fields and the player uses the
 * Xlib macros; everything is answered locally:
 *   - one screen of XSTUB_WIDTH x XSTUB_HEIGHT (or DISPLAY_WIDTH/HEIGHT, else 640x480), depth 24 TrueColor;
 *   - windows are records in a table; map/configure/focus events are queued for windows that selected them;
 *   - the keyboard is read from evdev (/dev/input/event*, rescanned every 2 s so gptokeyb's uinput keyboard is
 *     found whenever it appears); KeyPress/KeyRelease carry X keycodes (evdev + 8) and XLookupString maps them to
 *     keysyms with a US layout table; autorepeat is never delivered (the player asks for detectable autorepeat);
 *   - crusty's own calls (XOpenDisplay, XGetVisualInfo, XDefaultDepth, XQueryPointer, XFree) are served too, and
 *     crusty presents through SDL2 on its own, so no drawing ever happens here;
 *   - properties, selections, cursors, GCs, pixmaps and images are accepted and remembered just enough to be
 *     returned; nothing is drawn.
 * XSTUB_LOG=1 prints every call to stderr (which the launcher already collects).
 * Build: see build.sh (aarch64 cross compile, no dependencies beyond libc/libpthread).
 */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------- logging */
static int logon = -1;
static void LOG(const char *fmt, ...)
{
    if (logon < 0) logon = getenv("XSTUB_LOG") ? 1 : 0;
    if (!logon) return;
    va_list ap; va_start(ap, fmt);
    fputs("[xstub] ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}

/* rate-limited log of hot calls: the first 5 and then every 1000th */
#define LOGN(name) do { static int n_; n_++; if (n_ <= 5 || n_ % 1000 == 0) LOG("%s #%d (queue %d)", name, n_, qlen); } while (0)

/* ---------------------------------------------------------------- state */
static pthread_mutex_t lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#define L() pthread_mutex_lock(&lock)
#define U() pthread_mutex_unlock(&lock)

static Display *the_dpy;
static Screen the_screen;
static Visual the_visual;
static Depth the_depth;
static ScreenFormat the_format;
static int scr_w = 640, scr_h = 480;
static XID next_id = 0x400001;
static int evpipe[2] = {-1, -1};
static int focus_win;              /* index into wins */
static Time t0ms;

#define ROOT_WIN 0x1cb
#define MAXWIN 64
typedef struct { XID id, parent; int x, y, w, h, bw, depth, mapped, override; long mask; Visual *vis; Colormap cmap; char *name; Cursor cur; } win_t;
static win_t wins[MAXWIN];
static int nwins;

typedef struct qev { XEvent ev; struct qev *next; } qev_t;
static qev_t *qhead, *qtail;
static int qlen;

/* atoms: predefined 1..68 are Xatom.h; ours start at 100 */
#define MAXATOM 256
static char *atoms[MAXATOM];
static int natoms = 100;

/* properties (window, atom) -> data */
typedef struct prop { XID win; Atom atom, type; int format; unsigned char *data; unsigned long n; struct prop *next; } prop_t;
static prop_t *props;

/* evdev keyboards */
#define MAXKBD 16
static int kbd_fd[MAXKBD], nkbd;
static char kbd_name[MAXKBD][64];
static time_t last_scan;
#define MAXSEEN 64
static char seen_name[MAXSEEN][32]; static time_t seen_at[MAXSEEN]; static int nseen;   /* nodes looked at and rejected */
static unsigned char keydown[256];
static unsigned int modstate;

static Time now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (Time)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000) - t0ms;
}

static win_t *new_win(XID parent, int x, int y, int w, int h, int bw, int depth, Visual *vis);
static void configured(win_t *r);
static win_t *find_win(XID id)
{
    for (int i = 0; i < nwins; i++) if (wins[i].id == id) return &wins[i];
    return NULL;
}

/* queue an event; the pipe byte lets a select() on ConnectionNumber() wake up */
static void push(XEvent *ev)
{
    qev_t *q = calloc(1, sizeof *q); q->ev = *ev;
    if (qtail) qtail->next = q; else qhead = q;
    qtail = q; qlen++;
    if (the_dpy) the_dpy->qlen = qlen;
    if (qlen == 1 && evpipe[1] >= 0) { char c = 1; if (write(evpipe[1], &c, 1) < 0) {} }
}

static int pop(XEvent *ev)
{
    if (!qhead) return 0;
    qev_t *q = qhead; qhead = q->next; if (!qhead) qtail = NULL;
    *ev = q->ev; free(q); qlen--;
    if (the_dpy) the_dpy->qlen = qlen;
    if (qlen == 0 && evpipe[0] >= 0) { char c; while (read(evpipe[0], &c, 1) > 0) {} }
    return 1;
}

static void send_to(win_t *w, long mask, XEvent *ev)
{
    if (!w || !(w->mask & mask)) return;
    ev->xany.display = the_dpy; ev->xany.window = w->id; ev->xany.serial = ++the_dpy->request;
    push(ev);
}

/* ---------------------------------------------------------------- evdev keyboards */
/* raw syscalls: the preloaded crusty/gl4es libraries hook libc file functions in this process, and the hooked
 * open() failed for /dev/input nodes with a stale errno */
static int ev_open(const char *path) { return (int)syscall(SYS_openat, AT_FDCWD, path, O_RDONLY | O_NONBLOCK | O_CLOEXEC); }
static long ev_read(int fd, void *buf, size_t n) { return syscall(SYS_read, fd, buf, n); }
static int ev_ioctl(int fd, unsigned long req, void *arg) { return (int)syscall(SYS_ioctl, fd, req, arg); }
static int ev_close(int fd) { return (int)syscall(SYS_close, fd); }
static int ev_poll(struct pollfd *p, int n, int ms) { struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L }; return (int)syscall(SYS_ppoll, p, n, &ts, NULL, 0); }
static int is_keyboard(int fd)
{
    unsigned long evbits[(EV_MAX + 7) / 8 / sizeof(long) + 1] = {0};
    unsigned long keybits[(KEY_MAX + 7) / 8 / sizeof(long) + 1] = {0};
    if (ev_ioctl(fd, EVIOCGBIT(0, sizeof evbits), evbits) < 0) return 0;
    if (!(evbits[EV_KEY / (8 * sizeof(long))] & (1UL << (EV_KEY % (8 * sizeof(long)))))) return 0;
    if (ev_ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keybits), keybits) < 0) return 0;
#define HAS(k) (keybits[(k) / (8 * sizeof(long))] & (1UL << ((k) % (8 * sizeof(long)))))
    /* gamepads (gptokeyb2 reads those): some pad drivers also declare KEY_UP..KEY_RIGHT for the D-pad */
    if (HAS(BTN_SOUTH) || HAS(BTN_EAST) || HAS(BTN_START) || HAS(BTN_SELECT) || HAS(BTN_JOYSTICK)) return 0;
    return HAS(KEY_ENTER) || HAS(KEY_Z) || HAS(KEY_UP) || HAS(KEY_ESC);
#undef HAS
}

static void scan_keyboards(void)
{
    time_t t = time(NULL);
    if (t - last_scan < 2) return;
    last_scan = t;
    /* drop devices that went away (a virtual keyboard is destroyed and recreated under the same node name) */
    for (int i = 0; i < nkbd; i++) {
        int ver = 0;
        if (ev_ioctl(kbd_fd[i], EVIOCGVERSION, &ver) < 0) { LOG("keyboard %s gone", kbd_name[i]); ev_close(kbd_fd[i]); kbd_fd[i] = kbd_fd[nkbd - 1]; strcpy(kbd_name[i], kbd_name[nkbd - 1]); nkbd--; i--; }
    }
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    DIR *d = opendir("/dev/input"); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5)) continue;
        int dup = 0;
        for (int i = 0; i < nkbd; i++) if (!strcmp(kbd_name[i], e->d_name)) dup = 1;
        /* nodes rejected earlier (pads, power keys, mice) are not reopened every scan: opening evdev nodes is
           slow on some kernels and showed as a stall every 2 s; they are looked at again after 20 s */
        for (int i = 0; i < nseen && !dup; i++) if (!strcmp(seen_name[i], e->d_name)) { if (t - seen_at[i] < 20) dup = 1; else seen_at[i] = t; }
        if (dup || nkbd >= MAXKBD) continue;
        char path[80]; snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = ev_open(path);
        if (fd < 0) { LOG("scan: %s: open failed (%d)", e->d_name, fd); continue; }
        if (!is_keyboard(fd)) {
            ev_close(fd); int k = -1;
            for (int i = 0; i < nseen; i++) if (!strcmp(seen_name[i], e->d_name)) k = i;
            if (k < 0 && nseen < MAXSEEN) { k = nseen++; strncpy(seen_name[k], e->d_name, 31); }
            if (k >= 0) seen_at[k] = t;
            continue;
        }
        char name[64] = "?"; ev_ioctl(fd, EVIOCGNAME(sizeof name), name);
        kbd_fd[nkbd] = fd; strncpy(kbd_name[nkbd], e->d_name, 63); nkbd++;
        LOG("keyboard %s: %s", e->d_name, name);
    }
    closedir(d);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    if (ms > 10) LOG("scan took %ld ms", ms);
}

static void key_event(int code, int value)
{
    if (value == 2) return;                     /* autorepeat: detectable autorepeat = no repeats at all */
    if (code < 0 || code > 247) return;
    unsigned kc = code + 8;
    keydown[kc] = value ? 1 : 0;
    unsigned bit = 0;
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) bit = ShiftMask;
    else if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) bit = ControlMask;
    else if (code == KEY_LEFTALT || code == KEY_RIGHTALT) bit = Mod1Mask;
    win_t *w = nwins ? &wins[focus_win] : NULL;
    LOGN("key_event");
    XEvent ev; memset(&ev, 0, sizeof ev);
    ev.xkey.type = value ? KeyPress : KeyRelease;
    ev.xkey.root = ROOT_WIN; ev.xkey.time = now_ms(); ev.xkey.same_screen = True;
    ev.xkey.state = modstate; ev.xkey.keycode = kc;
    ev.xkey.x = ev.xkey.y = ev.xkey.x_root = ev.xkey.y_root = 0;
    if (bit) { if (value) modstate |= bit; else modstate &= ~bit; }
    if (w) send_to(w, value ? KeyPressMask : KeyReleaseMask, &ev);
}

static void read_keyboards(void)
{
    scan_keyboards();
    struct input_event ie[32];
    for (int i = 0; i < nkbd; i++) {
        for (;;) {
            long n = ev_read(kbd_fd[i], ie, sizeof ie);
            if (n <= 0) {   /* syscall() returns -1 and sets errno */
                if (n == 0 || errno != EAGAIN) { LOG("keyboard %s gone (%s)", kbd_name[i], n ? strerror(errno) : "eof"); ev_close(kbd_fd[i]); kbd_fd[i] = kbd_fd[nkbd - 1]; strcpy(kbd_name[i], kbd_name[nkbd - 1]); nkbd--; i--; }
                break;
            }
            for (int k = 0; k < (int)(n / sizeof ie[0]); k++)
                if (ie[k].type == EV_KEY) { LOGN("evdev key"); key_event(ie[k].code, ie[k].value); }
        }
    }
}

/* wait up to ms for a keyboard event (or forever when ms < 0) */
static void wait_input(int ms)
{
    struct pollfd p[MAXKBD];
    for (;;) {
        scan_keyboards();
        for (int i = 0; i < nkbd; i++) { p[i].fd = kbd_fd[i]; p[i].events = POLLIN; }
        int r = ev_poll(p, nkbd, nkbd ? (ms < 0 ? 500 : ms) : (ms < 0 ? 200 : ms));
        read_keyboards();
        if (qlen || r != 0 || ms >= 0) return;
    }
}

/* ---------------------------------------------------------------- keysyms (US layout) */
static const KeySym keymap[248] = {
    [KEY_ESC] = XK_Escape, [KEY_1] = XK_1, [KEY_2] = XK_2, [KEY_3] = XK_3, [KEY_4] = XK_4, [KEY_5] = XK_5,
    [KEY_6] = XK_6, [KEY_7] = XK_7, [KEY_8] = XK_8, [KEY_9] = XK_9, [KEY_0] = XK_0, [KEY_MINUS] = XK_minus,
    [KEY_EQUAL] = XK_equal, [KEY_BACKSPACE] = XK_BackSpace, [KEY_TAB] = XK_Tab, [KEY_Q] = XK_q, [KEY_W] = XK_w,
    [KEY_E] = XK_e, [KEY_R] = XK_r, [KEY_T] = XK_t, [KEY_Y] = XK_y, [KEY_U] = XK_u, [KEY_I] = XK_i, [KEY_O] = XK_o,
    [KEY_P] = XK_p, [KEY_LEFTBRACE] = XK_bracketleft, [KEY_RIGHTBRACE] = XK_bracketright, [KEY_ENTER] = XK_Return,
    [KEY_LEFTCTRL] = XK_Control_L, [KEY_A] = XK_a, [KEY_S] = XK_s, [KEY_D] = XK_d, [KEY_F] = XK_f, [KEY_G] = XK_g,
    [KEY_H] = XK_h, [KEY_J] = XK_j, [KEY_K] = XK_k, [KEY_L] = XK_l, [KEY_SEMICOLON] = XK_semicolon,
    [KEY_APOSTROPHE] = XK_apostrophe, [KEY_GRAVE] = XK_grave, [KEY_LEFTSHIFT] = XK_Shift_L, [KEY_BACKSLASH] = XK_backslash,
    [KEY_Z] = XK_z, [KEY_X] = XK_x, [KEY_C] = XK_c, [KEY_V] = XK_v, [KEY_B] = XK_b, [KEY_N] = XK_n, [KEY_M] = XK_m,
    [KEY_COMMA] = XK_comma, [KEY_DOT] = XK_period, [KEY_SLASH] = XK_slash, [KEY_RIGHTSHIFT] = XK_Shift_R,
    [KEY_KPASTERISK] = XK_KP_Multiply, [KEY_LEFTALT] = XK_Alt_L, [KEY_SPACE] = XK_space, [KEY_CAPSLOCK] = XK_Caps_Lock,
    [KEY_F1] = XK_F1, [KEY_F2] = XK_F2, [KEY_F3] = XK_F3, [KEY_F4] = XK_F4, [KEY_F5] = XK_F5, [KEY_F6] = XK_F6,
    [KEY_F7] = XK_F7, [KEY_F8] = XK_F8, [KEY_F9] = XK_F9, [KEY_F10] = XK_F10, [KEY_NUMLOCK] = XK_Num_Lock,
    [KEY_SCROLLLOCK] = XK_Scroll_Lock, [KEY_KP7] = XK_KP_7, [KEY_KP8] = XK_KP_8, [KEY_KP9] = XK_KP_9,
    [KEY_KPMINUS] = XK_KP_Subtract, [KEY_KP4] = XK_KP_4, [KEY_KP5] = XK_KP_5, [KEY_KP6] = XK_KP_6, [KEY_KPPLUS] = XK_KP_Add,
    [KEY_KP1] = XK_KP_1, [KEY_KP2] = XK_KP_2, [KEY_KP3] = XK_KP_3, [KEY_KP0] = XK_KP_0, [KEY_KPDOT] = XK_KP_Decimal,
    [KEY_F11] = XK_F11, [KEY_F12] = XK_F12, [KEY_KPENTER] = XK_KP_Enter, [KEY_RIGHTCTRL] = XK_Control_R,
    [KEY_KPSLASH] = XK_KP_Divide, [KEY_SYSRQ] = XK_Sys_Req, [KEY_RIGHTALT] = XK_Alt_R, [KEY_HOME] = XK_Home,
    [KEY_UP] = XK_Up, [KEY_PAGEUP] = XK_Page_Up, [KEY_LEFT] = XK_Left, [KEY_RIGHT] = XK_Right, [KEY_END] = XK_End,
    [KEY_DOWN] = XK_Down, [KEY_PAGEDOWN] = XK_Page_Down, [KEY_INSERT] = XK_Insert, [KEY_DELETE] = XK_Delete,
    [KEY_PAUSE] = XK_Pause, [KEY_LEFTMETA] = XK_Super_L, [KEY_RIGHTMETA] = XK_Super_R, [KEY_MENU] = XK_Menu,
};
static const char shifted[] = ")!@#$%^&*(";

static KeySym code_to_sym(unsigned kc, unsigned state)
{
    if (kc < 8 || kc - 8 >= 248) return NoSymbol;
    KeySym s = keymap[kc - 8];
    if (state & ShiftMask) {
        if (s >= XK_a && s <= XK_z) s -= 0x20;
        else if (s >= XK_0 && s <= XK_9) s = (unsigned char)shifted[s - XK_0];
    }
    return s;
}

/* ---------------------------------------------------------------- display */
static void init_atoms(void)
{
    static const char *pre[] = { "PRIMARY", "SECONDARY", "ARC", "ATOM", "BITMAP", "CARDINAL", "COLORMAP", "CURSOR",
        "CUT_BUFFER0", "CUT_BUFFER1", "CUT_BUFFER2", "CUT_BUFFER3", "CUT_BUFFER4", "CUT_BUFFER5", "CUT_BUFFER6",
        "CUT_BUFFER7", "DRAWABLE", "FONT", "INTEGER", "PIXMAP", "POINT", "RECTANGLE", "RESOURCE_MANAGER", "RGB_COLOR_MAP",
        "RGB_BEST_MAP", "RGB_BLUE_MAP", "RGB_DEFAULT_MAP", "RGB_GRAY_MAP", "RGB_GREEN_MAP", "RGB_RED_MAP", "STRING",
        "VISUALID", "WINDOW", "WM_COMMAND", "WM_HINTS", "WM_CLIENT_MACHINE", "WM_ICON_NAME", "WM_ICON_SIZE", "WM_NAME",
        "WM_NORMAL_HINTS", "WM_SIZE_HINTS", "WM_ZOOM_HINTS", "MIN_SPACE", "NORM_SPACE", "MAX_SPACE", "END_SPACE",
        "SUPERSCRIPT_X", "SUPERSCRIPT_Y", "SUBSCRIPT_X", "SUBSCRIPT_Y", "UNDERLINE_POSITION", "UNDERLINE_THICKNESS",
        "STRIKEOUT_ASCENT", "STRIKEOUT_DESCENT", "ITALIC_ANGLE", "X_HEIGHT", "QUAD_WIDTH", "WEIGHT", "POINT_SIZE",
        "RESOLUTION", "COPYRIGHT", "NOTICE", "FONT_NAME", "FAMILY_NAME", "FULL_NAME", "CAP_HEIGHT", "WM_CLASS",
        "WM_TRANSIENT_FOR" };
    for (int i = 0; i < 68; i++) atoms[i + 1] = (char *)pre[i];
}

Display *XOpenDisplay(const char *name)
{
    L();
    if (the_dpy) { LOG("XOpenDisplay again -> same display"); U(); return the_dpy; }
    const char *e;
    if ((e = getenv("XSTUB_WIDTH")) || (e = getenv("DISPLAY_WIDTH"))) scr_w = atoi(e);
    if ((e = getenv("XSTUB_HEIGHT")) || (e = getenv("DISPLAY_HEIGHT"))) scr_h = atoi(e);
    if (scr_w < 64 || scr_h < 64) { scr_w = 640; scr_h = 480; }
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); t0ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (pipe2(evpipe, O_NONBLOCK | O_CLOEXEC) < 0) evpipe[0] = evpipe[1] = -1;
    init_atoms();

    Display *d = calloc(1, sizeof *d);
    d->fd = evpipe[0];
    d->proto_major_version = 11; d->proto_minor_version = 0;
    d->vendor = strdup("xstub");
    d->resource_base = 0x400000; d->resource_mask = 0x1fffff; d->resource_id = 1; d->resource_shift = 0;
    d->byte_order = LSBFirst; d->bitmap_unit = 32; d->bitmap_pad = 32; d->bitmap_bit_order = LSBFirst;
    d->nformats = 1;
    the_format.depth = 24; the_format.bits_per_pixel = 32; the_format.scanline_pad = 32;
    d->pixmap_format = &the_format;
    d->vnumber = XlibSpecificationRelease; d->release = 12000000;
    d->display_name = strdup(name ? name : ":0"); d->default_screen = 0; d->nscreens = 1;
    d->min_keycode = 8; d->max_keycode = 255;
    d->xdefaults = NULL; d->db = NULL;
    d->bufptr = d->buffer = malloc(4096); d->bufmax = d->buffer + 4096;

    the_visual.visualid = 0x21; the_visual.class = TrueColor;
    the_visual.red_mask = 0xff0000; the_visual.green_mask = 0xff00; the_visual.blue_mask = 0xff;
    the_visual.bits_per_rgb = 8; the_visual.map_entries = 256;
    the_depth.depth = 24; the_depth.nvisuals = 1; the_depth.visuals = &the_visual;
    the_screen.display = d; the_screen.root = ROOT_WIN; the_screen.width = scr_w; the_screen.height = scr_h;
    the_screen.mwidth = scr_w * 254 / 960; the_screen.mheight = scr_h * 254 / 960;   /* ~96 dpi */
    the_screen.ndepths = 1; the_screen.depths = &the_depth; the_screen.root_depth = 24; the_screen.root_visual = &the_visual;
    the_screen.default_gc = (GC)calloc(1, sizeof(struct _XGC));
    the_screen.cmap = 0x20; the_screen.white_pixel = 0xffffff; the_screen.black_pixel = 0;
    the_screen.max_maps = the_screen.min_maps = 1; the_screen.backing_store = NotUseful;
    the_screen.save_unders = False; the_screen.root_input_mask = 0;
    d->screens = &the_screen;
    the_dpy = d;

    nwins = 1; memset(&wins[0], 0, sizeof wins[0]);
    wins[0].id = ROOT_WIN; wins[0].w = scr_w; wins[0].h = scr_h; wins[0].depth = 24; wins[0].mapped = 1;
    wins[0].vis = &the_visual; wins[0].cmap = 0x20; wins[0].name = strdup("root");
    focus_win = 0;
    /* look like an EWMH window manager: _NET_SUPPORTING_WM_CHECK on the root and on a hidden WM window,
       _NET_SUPPORTED listing the state atoms the player asks for (otherwise Unity says "Current window manager
       doesn't support fullscreen" and re-maps its window every frame) */
    win_t *wm = new_win(ROOT_WIN, -1, -1, 1, 1, 0, 24, NULL); wm->name = strdup("xstub wm");
    Atom a_check = XInternAtom(d, "_NET_SUPPORTING_WM_CHECK", False), a_utf8 = XInternAtom(d, "UTF8_STRING", False);
    Atom a_name = XInternAtom(d, "_NET_WM_NAME", False), a_sup = XInternAtom(d, "_NET_SUPPORTED", False);
    Window wmid = wm->id;
    XChangeProperty(d, ROOT_WIN, a_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&wmid, 1);
    XChangeProperty(d, wmid, a_check, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&wmid, 1);
    XChangeProperty(d, wmid, a_name, a_utf8, 8, PropModeReplace, (unsigned char *)"xstub", 5);
    static const char *sup[] = { "_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK", "_NET_WM_NAME", "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN",
        "_NET_WM_STATE_ABOVE", "_NET_ACTIVE_WINDOW", "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_ALLOWED_ACTIONS",
        "_NET_WM_ACTION_FULLSCREEN", "_NET_WM_ACTION_CLOSE", "_NET_CLOSE_WINDOW", "_NET_WM_PID", "_NET_CLIENT_LIST", "_NET_NUMBER_OF_DESKTOPS", "_NET_CURRENT_DESKTOP" };
    Atom supa[32]; int nsup = 0;
    for (int i = 0; i < (int)(sizeof sup / sizeof *sup); i++) supa[nsup++] = XInternAtom(d, sup[i], False);
    XChangeProperty(d, ROOT_WIN, a_sup, XA_ATOM, 32, PropModeReplace, (unsigned char *)supa, nsup);
    last_scan = 0; scan_keyboards();
    LOG("XOpenDisplay(%s): %dx%d, %d keyboards", name ? name : "(null)", scr_w, scr_h, nkbd);
    U();
    return d;
}

int XCloseDisplay(Display *d) { LOG("XCloseDisplay"); return 0; }
int XFlush(Display *d) { return 0; }
int XSync(Display *d, Bool discard) { L(); read_keyboards(); if (discard) { XEvent e; while (pop(&e)) {} } U(); return 0; }
int XBell(Display *d, int pct) { return 0; }
int XFree(void *p) { free(p); return 1; }
int XNoOp(Display *d) { return 0; }
int XInitThreads(void) { return 1; }
int XForceScreenSaver(Display *d, int m) { return 0; }
int XSetScreenSaver(Display *d, int a, int b, int c, int e) { return 0; }
int XGetScreenSaver(Display *d, int *a, int *b, int *c, int *e) { *a = 0; *b = 0; *c = 0; *e = 0; return 0; }
int (*XSynchronize(Display *d, int onoff))(Display *) { return NULL; }
int (*XSetAfterFunction(Display *d, int (*f)(Display *)))(Display *) { return NULL; }
XErrorHandler XSetErrorHandler(XErrorHandler h) { LOG("XSetErrorHandler"); return NULL; }
XIOErrorHandler XSetIOErrorHandler(XIOErrorHandler h) { return NULL; }
int XGetErrorText(Display *d, int code, char *buf, int len) { snprintf(buf, len, "xstub error %d", code); return 0; }
int XGetErrorDatabaseText(Display *d, const char *n, const char *m, const char *def, char *buf, int len) { snprintf(buf, len, "%s", def ? def : ""); return 0; }
char *XDisplayName(const char *s) { return (char *)(s ? s : ":0"); }
char *XDisplayString(Display *d) { return d->display_name; }
char *XServerVendor(Display *d) { return d->vendor; }
int XVendorRelease(Display *d) { return d->release; }
int XProtocolVersion(Display *d) { return 11; }
int XProtocolRevision(Display *d) { return 0; }
int XConnectionNumber(Display *d) { return d->fd; }
int XDefaultScreen(Display *d) { return 0; }
int XScreenCount(Display *d) { return 1; }
Screen *XScreenOfDisplay(Display *d, int s) { return &the_screen; }
Screen *XDefaultScreenOfDisplay(Display *d) { return &the_screen; }
Window XDefaultRootWindow(Display *d) { return ROOT_WIN; }
Window XRootWindow(Display *d, int s) { return ROOT_WIN; }
Window XRootWindowOfScreen(Screen *s) { return ROOT_WIN; }
int XDisplayWidth(Display *d, int s) { return scr_w; }
int XDisplayHeight(Display *d, int s) { return scr_h; }
int XDisplayWidthMM(Display *d, int s) { return the_screen.mwidth; }
int XDisplayHeightMM(Display *d, int s) { return the_screen.mheight; }
int XWidthOfScreen(Screen *s) { return scr_w; }
int XHeightOfScreen(Screen *s) { return scr_h; }
int XDefaultDepth(Display *d, int s) { return 24; }
int XDefaultDepthOfScreen(Screen *s) { return 24; }
int XDisplayPlanes(Display *d, int s) { return 24; }
int XDisplayCells(Display *d, int s) { return 256; }
Visual *XDefaultVisual(Display *d, int s) { return &the_visual; }
Visual *XDefaultVisualOfScreen(Screen *s) { return &the_visual; }
Colormap XDefaultColormap(Display *d, int s) { return 0x20; }
Colormap XDefaultColormapOfScreen(Screen *s) { return 0x20; }
GC XDefaultGC(Display *d, int s) { return the_screen.default_gc; }
GC XDefaultGCOfScreen(Screen *s) { return the_screen.default_gc; }
unsigned long XBlackPixel(Display *d, int s) { return 0; }
unsigned long XWhitePixel(Display *d, int s) { return 0xffffff; }
unsigned long XBlackPixelOfScreen(Screen *s) { return 0; }
unsigned long XWhitePixelOfScreen(Screen *s) { return 0xffffff; }
int XScreenNumberOfScreen(Screen *s) { return 0; }
Display *XDisplayOfScreen(Screen *s) { return the_dpy; }
long XMaxRequestSize(Display *d) { return 65535; }
long XExtendedMaxRequestSize(Display *d) { return 4194303; }
int XBitmapUnit(Display *d) { return 32; }
int XBitmapPad(Display *d) { return 32; }
int XBitmapBitOrder(Display *d) { return LSBFirst; }
int XImageByteOrder(Display *d) { return LSBFirst; }
char *XResourceManagerString(Display *d) { return NULL; }
char *XScreenResourceString(Screen *s) { return NULL; }
char *XGetDefault(Display *d, const char *prog, const char *opt) { return NULL; }
Bool XSupportsLocale(void) { return True; }
char *XSetLocaleModifiers(const char *m) { return ""; }
int XDisplayKeycodes(Display *d, int *min, int *max) { *min = 8; *max = 255; return 1; }
VisualID XVisualIDFromVisual(Visual *v) { return v ? v->visualid : 0; }
int XQueryExtension(Display *d, const char *name, int *op, int *ev, int *err) { LOG("XQueryExtension(%s)", name); *op = *ev = *err = 0; return False; }
char **XListExtensions(Display *d, int *n) { *n = 0; return NULL; }
int XFreeExtensionList(char **l) { return 1; }

/* ---------------------------------------------------------------- visuals */
XVisualInfo *XGetVisualInfo(Display *d, long mask, XVisualInfo *tpl, int *n)
{
    if ((mask & VisualScreenMask) && tpl->screen != 0) { *n = 0; return NULL; }
    if ((mask & VisualDepthMask) && tpl->depth != 24) { *n = 0; return NULL; }
    if ((mask & VisualClassMask) && tpl->class != TrueColor) { *n = 0; return NULL; }
    if ((mask & VisualIDMask) && tpl->visualid != the_visual.visualid) LOG("XGetVisualInfo: id 0x%lx asked, giving 0x%lx", (long)tpl->visualid, (long)the_visual.visualid);
    XVisualInfo *v = calloc(1, sizeof *v);
    v->visual = &the_visual; v->visualid = the_visual.visualid; v->screen = 0; v->depth = 24; v->class = TrueColor;
    v->red_mask = 0xff0000; v->green_mask = 0xff00; v->blue_mask = 0xff; v->colormap_size = 256; v->bits_per_rgb = 8;
    *n = 1;
    LOG("XGetVisualInfo(mask 0x%lx) -> 1", mask);
    return v;
}
Status XMatchVisualInfo(Display *d, int s, int depth, int class, XVisualInfo *v)
{
    if (depth != 24 || class != TrueColor) return 0;
    int n; XVisualInfo *r = XGetVisualInfo(d, 0, NULL, &n); *v = *r; free(r); return 1;
}

/* ---------------------------------------------------------------- windows */
static win_t *new_win(XID parent, int x, int y, int w, int h, int bw, int depth, Visual *vis)
{
    if (nwins >= MAXWIN) return NULL;
    win_t *r = &wins[nwins++]; memset(r, 0, sizeof *r);
    r->id = next_id++; r->parent = parent; r->x = x; r->y = y; r->w = w; r->h = h; r->bw = bw;
    r->depth = depth ? depth : 24; r->vis = vis ? vis : &the_visual; r->cmap = 0x20; r->name = strdup("");
    return r;
}

Window XCreateWindow(Display *d, Window parent, int x, int y, unsigned w, unsigned h, unsigned bw, int depth,
                     unsigned class, Visual *vis, unsigned long vmask, XSetWindowAttributes *a)
{
    L();
    win_t *r = new_win(parent, x, y, w, h, bw, depth, vis);
    if (r && a) {
        if (vmask & CWEventMask) r->mask = a->event_mask;
        if (vmask & CWOverrideRedirect) r->override = a->override_redirect;
        if (vmask & CWColormap) r->cmap = a->colormap;
        if (vmask & CWCursor) r->cur = a->cursor;
    }
    LOG("XCreateWindow(parent 0x%lx, %d,%d %ux%u, depth %d, mask 0x%lx) -> 0x%lx", parent, x, y, w, h, depth, r ? r->mask : 0, r ? r->id : 0);
    U();
    return r ? r->id : 0;
}
Window XCreateSimpleWindow(Display *d, Window parent, int x, int y, unsigned w, unsigned h, unsigned bw, unsigned long border, unsigned long bg)
{
    L(); win_t *r = new_win(parent, x, y, w, h, bw, 24, NULL); LOG("XCreateSimpleWindow -> 0x%lx", r ? r->id : 0); U(); return r ? r->id : 0;
}
int XDestroyWindow(Display *d, Window w)
{
    L(); win_t *r = find_win(w);
    if (r) { XEvent ev; memset(&ev, 0, sizeof ev); ev.xdestroywindow.type = DestroyNotify; ev.xdestroywindow.event = w; ev.xdestroywindow.window = w; send_to(r, StructureNotifyMask, &ev); r->id = 0; }
    LOG("XDestroyWindow(0x%lx)", w); U(); return 0;
}
int XMapWindow(Display *d, Window w)
{
    L(); win_t *r = find_win(w);
    if (r && !r->mapped) {
        r->mapped = 1; XEvent ev;
        configured(r);
        memset(&ev, 0, sizeof ev); ev.xmap.type = MapNotify; ev.xmap.event = w; ev.xmap.window = w; ev.xmap.override_redirect = r->override; send_to(r, StructureNotifyMask, &ev);
        memset(&ev, 0, sizeof ev); ev.xvisibility.type = VisibilityNotify; ev.xvisibility.window = w; ev.xvisibility.state = VisibilityUnobscured; send_to(r, VisibilityChangeMask, &ev);
        configured(r);
        memset(&ev, 0, sizeof ev); ev.xexpose.type = Expose; ev.xexpose.window = w; ev.xexpose.width = r->w; ev.xexpose.height = r->h; send_to(r, ExposureMask, &ev);
        focus_win = (int)(r - wins);
        memset(&ev, 0, sizeof ev); ev.xfocus.type = FocusIn; ev.xfocus.window = w; ev.xfocus.mode = NotifyNormal; ev.xfocus.detail = NotifyNonlinear; send_to(r, FocusChangeMask, &ev);
    }
    LOG("XMapWindow(0x%lx)", w); U(); return 0;
}
int XMapRaised(Display *d, Window w) { return XMapWindow(d, w); }
int XMapSubwindows(Display *d, Window w) { return 0; }
int XUnmapWindow(Display *d, Window w)
{
    L(); win_t *r = find_win(w);
    if (r && r->mapped) { r->mapped = 0; XEvent ev; memset(&ev, 0, sizeof ev); ev.xunmap.type = UnmapNotify; ev.xunmap.event = w; ev.xunmap.window = w; send_to(r, StructureNotifyMask, &ev); }
    LOG("XUnmapWindow(0x%lx)", w); U(); return 0;
}
static void configured(win_t *r)
{
    XEvent ev; memset(&ev, 0, sizeof ev); ev.xconfigure.type = ConfigureNotify; ev.xconfigure.event = r->id; ev.xconfigure.window = r->id;
    ev.xconfigure.x = r->x; ev.xconfigure.y = r->y; ev.xconfigure.width = r->w; ev.xconfigure.height = r->h; ev.xconfigure.border_width = r->bw;
    if (r->mapped) send_to(r, StructureNotifyMask, &ev);
}
int XMoveResizeWindow(Display *d, Window w, int x, int y, unsigned wd, unsigned ht) { L(); win_t *r = find_win(w); if (r) { r->x = x; r->y = y; r->w = wd; r->h = ht; configured(r); } LOG("XMoveResizeWindow(0x%lx, %d,%d %ux%u)", w, x, y, wd, ht); U(); return 0; }
int XMoveWindow(Display *d, Window w, int x, int y) { L(); win_t *r = find_win(w); if (r) { r->x = x; r->y = y; configured(r); } U(); return 0; }
int XResizeWindow(Display *d, Window w, unsigned wd, unsigned ht) { L(); win_t *r = find_win(w); if (r) { r->w = wd; r->h = ht; configured(r); } LOG("XResizeWindow(0x%lx, %ux%u)", w, wd, ht); U(); return 0; }
int XConfigureWindow(Display *d, Window w, unsigned m, XWindowChanges *c)
{
    L(); win_t *r = find_win(w);
    if (r) { if (m & CWX) r->x = c->x; if (m & CWY) r->y = c->y; if (m & CWWidth) r->w = c->width; if (m & CWHeight) r->h = c->height; if (m & CWBorderWidth) r->bw = c->border_width; configured(r); }
    U(); return 0;
}
int XRaiseWindow(Display *d, Window w) { L(); win_t *r = find_win(w); if (r) configured(r); LOG("XRaiseWindow(0x%lx)", w); U(); return 0; }
int XLowerWindow(Display *d, Window w) { L(); win_t *r = find_win(w); if (r) configured(r); U(); return 0; }
int XReparentWindow(Display *d, Window w, Window p, int x, int y)
{
    L(); win_t *r = find_win(w);
    if (r) { r->parent = p; r->x = x; r->y = y; XEvent ev; memset(&ev, 0, sizeof ev); ev.xreparent.type = ReparentNotify; ev.xreparent.event = w; ev.xreparent.window = w; ev.xreparent.parent = p; ev.xreparent.x = x; ev.xreparent.y = y; send_to(r, StructureNotifyMask, &ev); }
    LOG("XReparentWindow(0x%lx -> 0x%lx)", w, p); U(); return 0;
}
int XSelectInput(Display *d, Window w, long mask) { L(); win_t *r = find_win(w); if (r) r->mask = mask; LOG("XSelectInput(0x%lx, 0x%lx)", w, mask); U(); return 0; }
int XChangeWindowAttributes(Display *d, Window w, unsigned long vmask, XSetWindowAttributes *a)
{
    L(); win_t *r = find_win(w);
    if (r) { if (vmask & CWEventMask) r->mask = a->event_mask; if (vmask & CWOverrideRedirect) r->override = a->override_redirect; if (vmask & CWCursor) r->cur = a->cursor; }
    U(); return 0;
}
int XSetWindowBackground(Display *d, Window w, unsigned long p) { return 0; }
int XSetWindowBackgroundPixmap(Display *d, Window w, Pixmap p) { return 0; }
int XSetWindowBorderWidth(Display *d, Window w, unsigned bw) { return 0; }
int XClearWindow(Display *d, Window w) { return 0; }
int XClearArea(Display *d, Window w, int x, int y, unsigned wd, unsigned ht, Bool e) { return 0; }
Status XGetWindowAttributes(Display *d, Window w, XWindowAttributes *a)
{
    L(); win_t *r = find_win(w); memset(a, 0, sizeof *a);
    if (r) {
        a->x = r->x; a->y = r->y; a->width = r->w; a->height = r->h; a->border_width = r->bw; a->depth = r->depth;
        a->visual = r->vis; a->root = ROOT_WIN; a->class = InputOutput; a->bit_gravity = ForgetGravity; a->win_gravity = NorthWestGravity;
        a->backing_store = NotUseful; a->colormap = r->cmap; a->map_installed = True; a->map_state = r->mapped ? IsViewable : IsUnmapped;
        a->all_event_masks = a->your_event_mask = r->mask; a->override_redirect = r->override; a->screen = &the_screen;
    }
    LOG("XGetWindowAttributes(0x%lx) -> %dx%d", w, a->width, a->height); U(); return r ? 1 : 0;
}
Status XGetGeometry(Display *d, Drawable w, Window *root, int *x, int *y, unsigned *wd, unsigned *ht, unsigned *bw, unsigned *depth)
{
    L(); win_t *r = find_win(w); *root = ROOT_WIN;
    if (r) { *x = r->x; *y = r->y; *wd = r->w; *ht = r->h; *bw = r->bw; *depth = r->depth; }
    else { *x = *y = 0; *wd = scr_w; *ht = scr_h; *bw = 0; *depth = 24; }
    LOG("XGetGeometry(0x%lx) -> %ux%u", w, *wd, *ht); U(); return 1;
}
Status XQueryTree(Display *d, Window w, Window *root, Window *parent, Window **children, unsigned *n)
{
    L(); *root = ROOT_WIN; win_t *r = find_win(w); *parent = (r && r->id != ROOT_WIN) ? r->parent : None;
    int c = 0; for (int i = 0; i < nwins; i++) if (wins[i].id && wins[i].parent == w) c++;
    Window *ch = c ? calloc(c, sizeof *ch) : NULL; int k = 0;
    for (int i = 0; i < nwins; i++) if (wins[i].id && wins[i].parent == w) ch[k++] = wins[i].id;
    *children = ch; *n = c; U(); return 1;
}
Bool XTranslateCoordinates(Display *d, Window src, Window dst, int x, int y, int *dx, int *dy, Window *child)
{
    L(); win_t *s = find_win(src), *t = find_win(dst); int ax = x, ay = y;
    if (s && s->id != ROOT_WIN) { ax += s->x; ay += s->y; }
    if (t && t->id != ROOT_WIN) { ax -= t->x; ay -= t->y; }
    *dx = ax; *dy = ay; *child = None; U(); return True;
}
int XSetInputFocus(Display *d, Window w, int revert, Time t)
{
    L(); win_t *r = find_win(w);
    if (r && (int)(r - wins) != focus_win) {
        XEvent ev; memset(&ev, 0, sizeof ev); ev.xfocus.type = FocusOut; ev.xfocus.window = wins[focus_win].id; ev.xfocus.mode = NotifyNormal; ev.xfocus.detail = NotifyNonlinear; send_to(&wins[focus_win], FocusChangeMask, &ev);
        focus_win = (int)(r - wins);
        memset(&ev, 0, sizeof ev); ev.xfocus.type = FocusIn; ev.xfocus.window = w; ev.xfocus.mode = NotifyNormal; ev.xfocus.detail = NotifyNonlinear; send_to(r, FocusChangeMask, &ev);
    }
    LOG("XSetInputFocus(0x%lx)", w); U(); return 0;
}
int XGetInputFocus(Display *d, Window *w, int *revert) { L(); LOGN("XGetInputFocus"); *w = (focus_win > 0 && wins[focus_win].mapped) ? wins[focus_win].id : PointerRoot; *revert = RevertToPointerRoot; U(); return 0; }
Colormap XCreateColormap(Display *d, Window w, Visual *v, int alloc) { return 0x20; }
int XFreeColormap(Display *d, Colormap c) { return 0; }
int XSetWindowColormap(Display *d, Window w, Colormap c) { return 0; }
int XInstallColormap(Display *d, Colormap c) { return 0; }
Status XAllocColor(Display *d, Colormap c, XColor *col) { col->pixel = ((col->red >> 8) << 16) | ((col->green >> 8) << 8) | (col->blue >> 8); return 1; }
Status XParseColor(Display *d, Colormap c, const char *s, XColor *col) { unsigned v = 0; if (s && *s == '#') v = strtoul(s + 1, NULL, 16); col->red = ((v >> 16) & 0xff) * 257; col->green = ((v >> 8) & 0xff) * 257; col->blue = (v & 0xff) * 257; col->pixel = v; return 1; }
Status XAllocNamedColor(Display *d, Colormap c, const char *s, XColor *scr, XColor *exact) { memset(scr, 0, sizeof *scr); *exact = *scr; return 1; }

/* ---------------------------------------------------------------- properties, atoms, WM hints */
Atom XInternAtom(Display *d, const char *name, Bool only)
{
    L();
    for (int i = 1; i < natoms; i++) if (atoms[i] && !strcmp(atoms[i], name)) { U(); return i; }
    if (only || natoms >= MAXATOM) { U(); return None; }
    atoms[natoms] = strdup(name); Atom a = natoms++;
    LOG("XInternAtom(%s) -> %ld", name, a); U(); return a;
}
Status XInternAtoms(Display *d, char **names, int n, Bool only, Atom *out) { for (int i = 0; i < n; i++) out[i] = XInternAtom(d, names[i], only); return 1; }
char *XGetAtomName(Display *d, Atom a) { return (a > 0 && a < (Atom)natoms && atoms[a]) ? strdup(atoms[a]) : NULL; }

static prop_t *find_prop(XID w, Atom a) { for (prop_t *p = props; p; p = p->next) if (p->win == w && p->atom == a) return p; return NULL; }
int XChangeProperty(Display *d, Window w, Atom prop, Atom type, int format, int mode, const unsigned char *data, int n)
{
    L(); prop_t *p = find_prop(w, prop);
    if (!p) { p = calloc(1, sizeof *p); p->win = w; p->atom = prop; p->next = props; props = p; }
    int bytes = n * (format / 8);
    if (mode == PropModeReplace || !p->data) { free(p->data); p->data = malloc(bytes + 1); memcpy(p->data, data, bytes); p->data[bytes] = 0; p->n = n; }
    else { int old = p->n * (format / 8); unsigned char *nd = malloc(old + bytes + 1); if (mode == PropModeAppend) { memcpy(nd, p->data, old); memcpy(nd + old, data, bytes); } else { memcpy(nd, data, bytes); memcpy(nd + bytes, p->data, old); } nd[old + bytes] = 0; free(p->data); p->data = nd; p->n += n; }
    p->type = type; p->format = format;
    LOG("XChangeProperty(0x%lx, %s, type %ld, fmt %d, %d items)", w, atoms[prop] ? atoms[prop] : "?", type, format, n);
    win_t *r = find_win(w);
    if (r) { XEvent ev; memset(&ev, 0, sizeof ev); ev.xproperty.type = PropertyNotify; ev.xproperty.window = w; ev.xproperty.atom = prop; ev.xproperty.state = PropertyNewValue; ev.xproperty.time = now_ms(); send_to(r, PropertyChangeMask, &ev); }
    U(); return 0;
}
int XDeleteProperty(Display *d, Window w, Atom prop) { return 0; }
int XGetWindowProperty(Display *d, Window w, Atom prop, long off, long len, Bool del, Atom req, Atom *type, int *format, unsigned long *n, unsigned long *after, unsigned char **data)
{
    L(); prop_t *p = find_prop(w, prop);
    if (!p) { *type = None; *format = 0; *n = 0; *after = 0; *data = NULL; LOG("XGetWindowProperty(0x%lx, %s) -> none", w, (prop < (Atom)natoms && atoms[prop]) ? atoms[prop] : "?"); U(); return Success; }
    int bytes = p->n * (p->format / 8); *type = p->type; *format = p->format; *n = p->n; *after = 0;
    *data = malloc(bytes + 1); memcpy(*data, p->data, bytes); (*data)[bytes] = 0;
    U(); return Success;
}
Atom *XListProperties(Display *d, Window w, int *n) { *n = 0; return NULL; }
int XSetStandardProperties(Display *d, Window w, const char *name, const char *icon, Pixmap p, char **argv, int argc, XSizeHints *h) { return XStoreName(d, w, name); }
int XStoreName(Display *d, Window w, const char *name) { L(); win_t *r = find_win(w); if (r) { free(r->name); r->name = strdup(name ? name : ""); } LOG("XStoreName(0x%lx, %s)", w, name); U(); return 0; }
Status XFetchName(Display *d, Window w, char **name) { L(); win_t *r = find_win(w); *name = r ? strdup(r->name) : NULL; U(); return r ? 1 : 0; }
int XSetIconName(Display *d, Window w, const char *n) { return 0; }
int XSetWMProtocols(Display *d, Window w, Atom *p, int n) { LOG("XSetWMProtocols(%d)", n); return 1; }
Status XGetWMProtocols(Display *d, Window w, Atom **p, int *n) { *p = NULL; *n = 0; return 0; }
void XSetWMProperties(Display *d, Window w, XTextProperty *name, XTextProperty *icon, char **argv, int argc, XSizeHints *sh, XWMHints *wh, XClassHint *ch) { if (name && name->value) XStoreName(d, w, (char *)name->value); }
void XSetWMNormalHints(Display *d, Window w, XSizeHints *h) { }
int XSetWMHints(Display *d, Window w, XWMHints *h) { return 0; }
void XSetWMName(Display *d, Window w, XTextProperty *t) { if (t && t->value) XStoreName(d, w, (char *)t->value); }
void XSetWMIconName(Display *d, Window w, XTextProperty *t) { }
Status XGetWMNormalHints(Display *d, Window w, XSizeHints *h, long *sup) { memset(h, 0, sizeof *h); *sup = 0; return 0; }
XWMHints *XGetWMHints(Display *d, Window w) { return NULL; }
int XSetClassHint(Display *d, Window w, XClassHint *h) { return 0; }
Status XGetClassHint(Display *d, Window w, XClassHint *h) { h->res_name = h->res_class = NULL; return 0; }
int XSetTransientForHint(Display *d, Window w, Window p) { return 0; }
int XSetNormalHints(Display *d, Window w, XSizeHints *h) { return 0; }
int XSetSizeHints(Display *d, Window w, XSizeHints *h, Atom p) { return 0; }
XSizeHints *XAllocSizeHints(void) { return calloc(1, sizeof(XSizeHints)); }
XWMHints *XAllocWMHints(void) { return calloc(1, sizeof(XWMHints)); }
XClassHint *XAllocClassHint(void) { return calloc(1, sizeof(XClassHint)); }
XStandardColormap *XAllocStandardColormap(void) { return calloc(1, sizeof(XStandardColormap)); }
XIconSize *XAllocIconSize(void) { return calloc(1, sizeof(XIconSize)); }
Status XStringListToTextProperty(char **list, int count, XTextProperty *t)
{
    size_t len = 0; for (int i = 0; i < count; i++) len += strlen(list[i]) + 1;
    char *v = malloc(len + 1); size_t o = 0;
    for (int i = 0; i < count; i++) { size_t l = strlen(list[i]); memcpy(v + o, list[i], l); o += l; v[o++] = 0; }
    v[o] = 0; t->value = (unsigned char *)v; t->encoding = XA_STRING; t->format = 8; t->nitems = len ? len - 1 : 0; return 1;
}
Status XTextPropertyToStringList(XTextProperty *t, char ***list, int *count) { *list = calloc(1, sizeof(char *)); (*list)[0] = strdup((char *)t->value); *count = 1; return 1; }
void XFreeStringList(char **l) { if (l) { free(l[0]); free(l); } }
int Xutf8TextListToTextProperty(Display *d, char **list, int count, XICCEncodingStyle st, XTextProperty *t) { return XStringListToTextProperty(list, count, t); }
int XmbTextListToTextProperty(Display *d, char **list, int count, XICCEncodingStyle st, XTextProperty *t) { return XStringListToTextProperty(list, count, t); }

/* ---------------------------------------------------------------- selections */
static Window sel_owner[MAXATOM];
int XSetSelectionOwner(Display *d, Atom sel, Window w, Time t) { if (sel < MAXATOM) sel_owner[sel] = w; return 0; }
Window XGetSelectionOwner(Display *d, Atom sel) { return sel < MAXATOM ? sel_owner[sel] : None; }
int XConvertSelection(Display *d, Atom sel, Atom target, Atom prop, Window req, Time t)
{
    /* nobody owns anything: answer with a failed SelectionNotify, which is what the player expects to handle */
    L(); win_t *r = find_win(req);
    if (r) { XEvent ev; memset(&ev, 0, sizeof ev); ev.xselection.type = SelectionNotify; ev.xselection.requestor = req; ev.xselection.selection = sel; ev.xselection.target = target; ev.xselection.property = None; ev.xselection.time = t;
             ev.xany.display = the_dpy; ev.xany.window = req; push(&ev); }
    LOG("XConvertSelection"); U(); return 0;
}

/* ---------------------------------------------------------------- events */
Status XSendEvent(Display *d, Window w, Bool prop, long mask, XEvent *ev)
{
    L();
    if (ev->type == ClientMessage) {
        Atom t = ev->xclient.message_type;
        LOG("XSendEvent(ClientMessage %s to 0x%lx, data %ld %ld %ld)", (t < (Atom)natoms && atoms[t]) ? atoms[t] : "?", w, ev->xclient.data.l[0], ev->xclient.data.l[1], ev->xclient.data.l[2]);
        /* _NET_WM_STATE add/remove/toggle: update the window's state property and size, as a WM would */
        if (t < (Atom)natoms && atoms[t] && !strcmp(atoms[t], "_NET_WM_STATE")) {
            win_t *r = find_win(ev->xclient.window);
            Atom a_state = XInternAtom(d, "_NET_WM_STATE", False), a_fs = XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False);
            for (int k = 1; k <= 2 && r; k++) {
                Atom a = ev->xclient.data.l[k]; if (!a) continue;
                prop_t *p = find_prop(r->id, a_state); Atom cur[16]; int n = 0, has = 0;
                if (p) { n = p->n > 16 ? 16 : p->n; memcpy(cur, p->data, n * sizeof(Atom)); }
                for (int i = 0; i < n; i++) if (cur[i] == a) has = 1;
                int want = ev->xclient.data.l[0] == 1 ? 1 : ev->xclient.data.l[0] == 0 ? 0 : !has;
                if (want && !has && n < 16) cur[n++] = a;
                if (!want && has) { int m = 0; for (int i = 0; i < n; i++) if (cur[i] != a) cur[m++] = cur[i]; n = m; }
                XChangeProperty(d, r->id, a_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)cur, n);
                if (a == a_fs) { if (want && (r->w != scr_w || r->h != scr_h || r->x || r->y)) { r->x = 0; r->y = 0; r->w = scr_w; r->h = scr_h; configured(r); } }
            }
        } else if (t < (Atom)natoms && atoms[t] && !strcmp(atoms[t], "_NET_ACTIVE_WINDOW")) {
            XSetInputFocus(d, ev->xclient.window, RevertToParent, CurrentTime);
        }
    } else { win_t *r = find_win(w); if (r) { XEvent e = *ev; e.xany.send_event = True; e.xany.display = the_dpy; push(&e); } }
    U(); return 1;
}
int XPending(Display *d) { L(); read_keyboards(); LOGN("XPending"); int n = qlen; U(); return n; }
int XEventsQueued(Display *d, int mode) { L(); if (mode != QueuedAlready) read_keyboards(); LOGN("XEventsQueued"); int n = qlen; U(); return n; }
int XNextEvent(Display *d, XEvent *ev)
{
    L(); read_keyboards(); LOGN("XNextEvent");
    while (!pop(ev)) { U(); wait_input(-1); L(); }
    U(); return 0;
}
int XPeekEvent(Display *d, XEvent *ev) { L(); read_keyboards(); while (!qhead) { U(); wait_input(-1); L(); } *ev = qhead->ev; U(); return 0; }
int XPutBackEvent(Display *d, XEvent *ev) { L(); qev_t *q = calloc(1, sizeof *q); q->ev = *ev; q->next = qhead; qhead = q; if (!qtail) qtail = q; qlen++; d->qlen = qlen; U(); return 0; }
static int take_matching(XEvent *ev, int (*match)(XEvent *, void *), void *arg)
{
    qev_t *prev = NULL;
    for (qev_t *q = qhead; q; prev = q, q = q->next)
        if (match(&q->ev, arg)) {
            if (prev) prev->next = q->next; else qhead = q->next;
            if (qtail == q) qtail = prev;
            *ev = q->ev; free(q); qlen--; the_dpy->qlen = qlen;
            if (qlen == 0 && evpipe[0] >= 0) { char c; while (read(evpipe[0], &c, 1) > 0) {} }
            return 1;
        }
    return 0;
}
struct tw { Window w; int type; long mask; };
static int m_typed_win(XEvent *e, void *a) { struct tw *t = a; return e->xany.window == t->w && e->type == t->type; }
static int m_typed(XEvent *e, void *a) { struct tw *t = a; return e->type == t->type; }
static const long type_masks[LASTEvent] = { [KeyPress] = KeyPressMask, [KeyRelease] = KeyReleaseMask, [ButtonPress] = ButtonPressMask, [ButtonRelease] = ButtonReleaseMask,
    [MotionNotify] = PointerMotionMask, [EnterNotify] = EnterWindowMask, [LeaveNotify] = LeaveWindowMask, [FocusIn] = FocusChangeMask, [FocusOut] = FocusChangeMask,
    [Expose] = ExposureMask, [VisibilityNotify] = VisibilityChangeMask, [DestroyNotify] = StructureNotifyMask, [UnmapNotify] = StructureNotifyMask, [MapNotify] = StructureNotifyMask,
    [ReparentNotify] = StructureNotifyMask, [ConfigureNotify] = StructureNotifyMask, [PropertyNotify] = PropertyChangeMask };
static int m_win_mask(XEvent *e, void *a) { struct tw *t = a; return e->xany.window == t->w && e->type < LASTEvent && (type_masks[e->type] & t->mask); }
static int m_mask(XEvent *e, void *a) { struct tw *t = a; return e->type < LASTEvent && (type_masks[e->type] & t->mask); }
Bool XCheckTypedWindowEvent(Display *d, Window w, int type, XEvent *ev) { struct tw t = { w, type, 0 }; L(); read_keyboards(); LOGN("XCheckTypedWindowEvent"); int r = take_matching(ev, m_typed_win, &t); U(); return r; }
Bool XCheckTypedEvent(Display *d, int type, XEvent *ev) { struct tw t = { 0, type, 0 }; L(); read_keyboards(); int r = take_matching(ev, m_typed, &t); U(); return r; }
Bool XCheckWindowEvent(Display *d, Window w, long mask, XEvent *ev) { struct tw t = { w, 0, mask }; L(); read_keyboards(); LOGN("XCheckWindowEvent"); int r = take_matching(ev, m_win_mask, &t); U(); return r; }
Bool XCheckMaskEvent(Display *d, long mask, XEvent *ev) { struct tw t = { 0, 0, mask }; L(); read_keyboards(); int r = take_matching(ev, m_mask, &t); U(); return r; }
int XWindowEvent(Display *d, Window w, long mask, XEvent *ev) { struct tw t = { w, 0, mask }; L(); read_keyboards(); while (!take_matching(ev, m_win_mask, &t)) { U(); wait_input(-1); L(); } U(); return 0; }
int XMaskEvent(Display *d, long mask, XEvent *ev) { struct tw t = { 0, 0, mask }; L(); read_keyboards(); while (!take_matching(ev, m_mask, &t)) { U(); wait_input(-1); L(); } U(); return 0; }
struct pw { Bool (*pred)(Display *, XEvent *, XPointer); XPointer arg; };
static int m_pred(XEvent *e, void *a) { struct pw *p = a; return p->pred(the_dpy, e, p->arg); }
int XIfEvent(Display *d, XEvent *ev, Bool (*pred)(Display *, XEvent *, XPointer), XPointer arg) { struct pw p = { pred, arg }; L(); read_keyboards(); while (!take_matching(ev, m_pred, &p)) { U(); wait_input(-1); L(); } U(); return 0; }
Bool XCheckIfEvent(Display *d, XEvent *ev, Bool (*pred)(Display *, XEvent *, XPointer), XPointer arg) { struct pw p = { pred, arg }; L(); read_keyboards(); int r = take_matching(ev, m_pred, &p); U(); return r; }
int XPeekIfEvent(Display *d, XEvent *ev, Bool (*pred)(Display *, XEvent *, XPointer), XPointer arg) { L(); read_keyboards(); for (;;) { for (qev_t *q = qhead; q; q = q->next) if (pred(d, &q->ev, arg)) { *ev = q->ev; U(); return 0; } U(); wait_input(-1); L(); } }
Bool XFilterEvent(XEvent *ev, Window w) { return False; }
Bool XkbSetDetectableAutoRepeat(Display *d, Bool det, Bool *sup) { if (sup) *sup = True; LOG("XkbSetDetectableAutoRepeat(%d)", det); return True; }
int XAutoRepeatOn(Display *d) { return 0; }
int XAutoRepeatOff(Display *d) { return 0; }
int XRefreshKeyboardMapping(XMappingEvent *e) { return 0; }
int XLookupString(XKeyEvent *ev, char *buf, int len, KeySym *sym, XComposeStatus *st)
{
    KeySym s = code_to_sym(ev->keycode, ev->state);
    LOGN("XLookupString");
    if (sym) *sym = s;
    int n = 0;
    if (s == XK_Return || s == XK_KP_Enter) { if (len > 0) buf[n++] = '\r'; }
    else if (s == XK_BackSpace) { if (len > 0) buf[n++] = 8; }
    else if (s == XK_Tab) { if (len > 0) buf[n++] = 9; }
    else if (s == XK_Escape) { if (len > 0) buf[n++] = 27; }
    else if (s >= 0x20 && s <= 0x7e) { if (len > 0) buf[n++] = (char)s; }
    return n;
}
KeySym XLookupKeysym(XKeyEvent *ev, int index) { return code_to_sym(ev->keycode, index ? ShiftMask : 0); }
KeySym XKeycodeToKeysym(Display *d, KeyCode kc, int index) { return code_to_sym(kc, index ? ShiftMask : 0); }
KeySym XkbKeycodeToKeysym(Display *d, KeyCode kc, int group, int level) { return code_to_sym(kc, level ? ShiftMask : 0); }
KeyCode XKeysymToKeycode(Display *d, KeySym s)
{
    KeySym l = (s >= XK_A && s <= XK_Z) ? s + 0x20 : s;
    for (int i = 0; i < 248; i++) if (keymap[i] == l) return i + 8;
    return 0;
}
KeySym *XGetKeyboardMapping(Display *d, KeyCode first, int count, int *per)
{
    *per = 2; KeySym *m = calloc(count * 2, sizeof(KeySym));
    for (int i = 0; i < count; i++) { m[i * 2] = code_to_sym(first + i, 0); m[i * 2 + 1] = code_to_sym(first + i, ShiftMask); }
    return m;
}
XModifierKeymap *XGetModifierMapping(Display *d)
{
    XModifierKeymap *m = calloc(1, sizeof *m); m->max_keypermod = 2; m->modifiermap = calloc(16, 1);
    m->modifiermap[0] = KEY_LEFTSHIFT + 8; m->modifiermap[1] = KEY_RIGHTSHIFT + 8; m->modifiermap[4] = KEY_LEFTCTRL + 8; m->modifiermap[5] = KEY_RIGHTCTRL + 8;
    m->modifiermap[6] = KEY_LEFTALT + 8; m->modifiermap[7] = KEY_RIGHTALT + 8; return m;
}
int XFreeModifiermap(XModifierKeymap *m) { free(m->modifiermap); free(m); return 1; }
KeySym XStringToKeysym(const char *s) { if (!s) return NoSymbol; if (strlen(s) == 1) return (unsigned char)s[0]; return NoSymbol; }
char *XKeysymToString(KeySym s) { static char b[16]; if (s >= 0x20 && s <= 0x7e) { b[0] = (char)s; b[1] = 0; return b; } return NULL; }
Status XQueryKeymap(Display *d, char keys[32]) { L(); read_keyboards(); memset(keys, 0, 32); for (int k = 8; k < 256; k++) if (keydown[k]) keys[k / 8] |= 1 << (k % 8); U(); return 1; }

/* ---------------------------------------------------------------- pointer, cursors */
int XGrabPointer(Display *d, Window w, Bool oe, unsigned mask, int pm, int km, Window conf, Cursor c, Time t) { LOG("XGrabPointer"); return GrabSuccess; }
int XUngrabPointer(Display *d, Time t) { return 0; }
int XGrabKeyboard(Display *d, Window w, Bool oe, int pm, int km, Time t) { return GrabSuccess; }
int XUngrabKeyboard(Display *d, Time t) { return 0; }
int XGrabServer(Display *d) { return 0; }
int XUngrabServer(Display *d) { return 0; }
int XWarpPointer(Display *d, Window src, Window dst, int sx, int sy, unsigned sw, unsigned sh, int dx, int dy) { return 0; }
Bool XQueryPointer(Display *d, Window w, Window *root, Window *child, int *rx, int *ry, int *wx, int *wy, unsigned *mask)
{
    *root = ROOT_WIN; *child = None; *rx = *ry = *wx = *wy = 0; *mask = modstate; return True;
}
Cursor XCreateFontCursor(Display *d, unsigned shape) { return next_id++; }
Cursor XCreatePixmapCursor(Display *d, Pixmap src, Pixmap mask, XColor *fg, XColor *bg, unsigned x, unsigned y) { return next_id++; }
Cursor XCreateGlyphCursor(Display *d, Font sf, Font mf, unsigned sc, unsigned mc, XColor const *fg, XColor const *bg) { return next_id++; }
int XFreeCursor(Display *d, Cursor c) { return 0; }
int XDefineCursor(Display *d, Window w, Cursor c) { L(); win_t *r = find_win(w); if (r) r->cur = c; U(); return 0; }
int XUndefineCursor(Display *d, Window w) { return XDefineCursor(d, w, None); }
int XRecolorCursor(Display *d, Cursor c, XColor *fg, XColor *bg) { return 0; }

/* ---------------------------------------------------------------- pixmaps, GCs, images (never drawn) */
Pixmap XCreatePixmap(Display *d, Drawable dr, unsigned w, unsigned h, unsigned depth) { return next_id++; }
int XFreePixmap(Display *d, Pixmap p) { return 0; }
Pixmap XCreateBitmapFromData(Display *d, Drawable dr, const char *data, unsigned w, unsigned h) { return next_id++; }
Pixmap XCreatePixmapFromBitmapData(Display *d, Drawable dr, char *data, unsigned w, unsigned h, unsigned long fg, unsigned long bg, unsigned depth) { return next_id++; }
GC XCreateGC(Display *d, Drawable dr, unsigned long mask, XGCValues *v) { GC g = calloc(1, sizeof(struct _XGC)); g->gid = next_id++; if (v && (mask & GCForeground)) g->values.foreground = v->foreground; return g; }
int XFreeGC(Display *d, GC g) { free(g); return 0; }
int XChangeGC(Display *d, GC g, unsigned long mask, XGCValues *v) { return 0; }
int XSetForeground(Display *d, GC g, unsigned long p) { return 0; }
int XSetBackground(Display *d, GC g, unsigned long p) { return 0; }
int XSetFunction(Display *d, GC g, int f) { return 0; }
int XSetFillStyle(Display *d, GC g, int f) { return 0; }
int XSetGraphicsExposures(Display *d, GC g, Bool e) { return 0; }
int XFillRectangle(Display *d, Drawable dr, GC g, int x, int y, unsigned w, unsigned h) { return 0; }
int XDrawRectangle(Display *d, Drawable dr, GC g, int x, int y, unsigned w, unsigned h) { return 0; }
int XCopyArea(Display *d, Drawable s, Drawable t, GC g, int sx, int sy, unsigned w, unsigned h, int dx, int dy) { return 0; }
int XDrawString(Display *d, Drawable dr, GC g, int x, int y, const char *s, int n) { return 0; }
int XDrawLine(Display *d, Drawable dr, GC g, int a, int b, int c, int e) { return 0; }
int XDrawPoint(Display *d, Drawable dr, GC g, int x, int y) { return 0; }

static unsigned long img_get_pixel(XImage *i, int x, int y)
{
    if (!i->data || x < 0 || y < 0 || x >= i->width || y >= i->height) return 0;
    unsigned char *p = (unsigned char *)i->data + y * i->bytes_per_line;
    if (i->bits_per_pixel == 32) return ((unsigned *)p)[x];
    if (i->bits_per_pixel == 16) return ((unsigned short *)p)[x];
    if (i->bits_per_pixel == 8) return p[x];
    if (i->bits_per_pixel == 1) return (p[x / 8] >> (x % 8)) & 1;
    return 0;
}
static int img_put_pixel(XImage *i, int x, int y, unsigned long v)
{
    if (!i->data || x < 0 || y < 0 || x >= i->width || y >= i->height) return 0;
    unsigned char *p = (unsigned char *)i->data + y * i->bytes_per_line;
    if (i->bits_per_pixel == 32) ((unsigned *)p)[x] = v;
    else if (i->bits_per_pixel == 16) ((unsigned short *)p)[x] = v;
    else if (i->bits_per_pixel == 8) p[x] = v;
    else if (i->bits_per_pixel == 1) { if (v) p[x / 8] |= 1 << (x % 8); else p[x / 8] &= ~(1 << (x % 8)); }
    return 0;
}
static int img_destroy(XImage *i) { free(i->data); free(i); return 1; }
static XImage *img_sub(XImage *i, int x, int y, unsigned w, unsigned h) { return NULL; }
static int img_add(XImage *i, long v) { return 0; }
static XImage *img_create(Display *d, Visual *v, unsigned depth, int fmt, int off, char *data, unsigned w, unsigned h, int pad, int bpl);
Status XInitImage(XImage *i)
{
    i->f.create_image = img_create; i->f.destroy_image = img_destroy; i->f.get_pixel = img_get_pixel; i->f.put_pixel = img_put_pixel; i->f.sub_image = img_sub; i->f.add_pixel = img_add;
    return 1;
}
static XImage *img_create(Display *d, Visual *v, unsigned depth, int fmt, int off, char *data, unsigned w, unsigned h, int pad, int bpl)
{
    XImage *i = calloc(1, sizeof *i);
    i->width = w; i->height = h; i->xoffset = off; i->format = fmt; i->data = data; i->byte_order = LSBFirst;
    i->bitmap_unit = 32; i->bitmap_bit_order = LSBFirst; i->bitmap_pad = pad ? pad : 32; i->depth = depth;
    i->bits_per_pixel = depth == 1 ? 1 : depth <= 8 ? 8 : depth <= 16 ? 16 : 32;
    i->bytes_per_line = bpl ? bpl : (int)(((w * i->bits_per_pixel + i->bitmap_pad - 1) / i->bitmap_pad) * (i->bitmap_pad / 8));
    if (depth >= 24) { i->red_mask = 0xff0000; i->green_mask = 0xff00; i->blue_mask = 0xff; }
    XInitImage(i); return i;
}
XImage *XCreateImage(Display *d, Visual *v, unsigned depth, int fmt, int off, char *data, unsigned w, unsigned h, int pad, int bpl) { LOG("XCreateImage(%ux%u depth %u)", w, h, depth); return img_create(d, v, depth, fmt, off, data, w, h, pad, bpl); }
#undef XDestroyImage
int XDestroyImage(XImage *i) { return img_destroy(i); }
int XPutImage(Display *d, Drawable dr, GC g, XImage *i, int sx, int sy, int dx, int dy, unsigned w, unsigned h) { return 0; }
XImage *XGetImage(Display *d, Drawable dr, int x, int y, unsigned w, unsigned h, unsigned long mask, int fmt) { char *data = calloc(w * 4, h); return img_create(d, &the_visual, 24, fmt, 0, data, w, h, 32, w * 4); }

/* ---------------------------------------------------------------- fonts (none) */
char **XListFonts(Display *d, const char *pat, int max, int *n) { *n = 0; LOG("XListFonts(%s)", pat); return NULL; }
int XFreeFontNames(char **l) { return 1; }
char **XGetFontPath(Display *d, int *n) { *n = 0; return NULL; }
int XFreeFontPath(char **l) { return 1; }
int XSetFontPath(Display *d, char **l, int n) { return 0; }
XFontStruct *XLoadQueryFont(Display *d, const char *name) { LOG("XLoadQueryFont(%s) -> none", name); return NULL; }
Font XLoadFont(Display *d, const char *name) { return next_id++; }
int XFreeFont(Display *d, XFontStruct *f) { return 0; }
int XUnloadFont(Display *d, Font f) { return 0; }
int XSetFont(Display *d, GC g, Font f) { return 0; }

/* ---------------------------------------------------------------- input methods (none) */
XIM XOpenIM(Display *d, struct _XrmHashBucketRec *db, char *res, char *cls) { return NULL; }
Status XCloseIM(XIM im) { return 0; }
XIC XCreateIC(XIM im, ...) { return NULL; }
void XDestroyIC(XIC ic) { }
void XSetICFocus(XIC ic) { }
void XUnsetICFocus(XIC ic) { }
Bool XRegisterIMInstantiateCallback(Display *d, struct _XrmHashBucketRec *db, char *res, char *cls, XIDProc cb, XPointer p) { return False; }
Bool XUnregisterIMInstantiateCallback(Display *d, struct _XrmHashBucketRec *db, char *res, char *cls, XIDProc cb, XPointer p) { return False; }
char *XGetIMValues(XIM im, ...) { return NULL; }
char *XSetIMValues(XIM im, ...) { return NULL; }
char *XSetICValues(XIC ic, ...) { return NULL; }
char *XGetICValues(XIC ic, ...) { return NULL; }
int Xutf8LookupString(XIC ic, XKeyPressedEvent *ev, char *buf, int len, KeySym *sym, Status *st) { if (st) *st = XLookupKeySym; return XLookupString(ev, buf, len, sym, NULL); }
int XmbLookupString(XIC ic, XKeyPressedEvent *ev, char *buf, int len, KeySym *sym, Status *st) { if (st) *st = XLookupKeySym; return XLookupString(ev, buf, len, sym, NULL); }
/* Xlib internals that extensions or box64 may touch: box64's libX11 wrapper dereferences the mutex function
 * pointers at load time, so they must exist and point at something callable */
static void noop_lock(LockInfoPtr l) { }
void (*_XLockMutex_fn)(LockInfoPtr) = noop_lock;
void (*_XUnlockMutex_fn)(LockInfoPtr) = noop_lock;
void (*_XCreateMutex_fn)(LockInfoPtr) = noop_lock;
void (*_XFreeMutex_fn)(LockInfoPtr) = noop_lock;
LockInfoPtr _Xglobal_lock = NULL;
int _Xdebug = 0;
void *_qfree = NULL;
XExtCodes *XInitExtension(Display *d, const char *name) { return NULL; }
XExtCodes *XAddExtension(Display *d) { return NULL; }
int (*XESetWireToEvent(Display *d, int n, int (*p)(Display *, XEvent *, xEvent *)))(Display *, XEvent *, xEvent *) { return NULL; }
Status (*XESetEventToWire(Display *d, int n, Status (*p)(Display *, XEvent *, xEvent *)))(Display *, XEvent *, xEvent *) { return NULL; }
int (*XESetCloseDisplay(Display *d, int n, int (*p)(Display *, XExtCodes *)))(Display *, XExtCodes *) { return NULL; }
int (*XESetError(Display *d, int n, int (*p)(Display *, xError *, XExtCodes *, int *)))(Display *, xError *, XExtCodes *, int *) { return NULL; }
void XLockDisplay(Display *d) { }
void XUnlockDisplay(Display *d) { }
Status XAddConnectionWatch(Display *d, XConnectionWatchProc p, XPointer a) { return 0; }
void XRemoveConnectionWatch(Display *d, XConnectionWatchProc p, XPointer a) { }
Bool XkbQueryExtension(Display *d, int *op, int *ev, int *err, int *maj, int *min) { return False; }
Bool XkbLibraryVersion(int *maj, int *min) { return False; }
int XkbGetState(Display *d, unsigned dev, XkbStatePtr s) { return 0; }
Status XGetIconSizes(Display *d, Window w, XIconSize **l, int *n) { *l = NULL; *n = 0; return 0; }
