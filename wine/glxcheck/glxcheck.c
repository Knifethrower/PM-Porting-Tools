/* glxcheck: native OpenGL/GLX check without box64 or Wine. Loads libGL.so.1 with dlopen (the one
 * the library path finds, as box64 would for Wine), prints the X server's GLX vendor/version, creates
 * a GLX context on a small window, makes it current and prints GL_VENDOR/RENDERER/VERSION.
 * Exit 0 = OpenGL works on this display. Build: see build.sh */
#define _GNU_SOURCE  /* dladdr */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#include <stdio.h>

typedef struct __GLXcontextRec *GLXContext;
#define GLX_RGBA 4
#define GLX_DOUBLEBUFFER 5
#define GLX_DEPTH_SIZE 12
#define GLX_VENDOR 1
#define GLX_VERSION 2
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02

int main(void)
{
    const char *lib = "libGL.so.1";
    void *gl = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!gl) { printf("glxcheck: FAIL dlopen %s: %s\n", lib, dlerror()); return 1; }
    Dl_info info;
    void *sym = dlsym(gl, "glXChooseVisual");
    if (sym && dladdr(sym, &info)) printf("glxcheck: libGL = %s\n", info.dli_fname);

    const char *(*QueryServerString)(Display *, int, int) = dlsym(gl, "glXQueryServerString");
    const char *(*GetClientString)(Display *, int) = dlsym(gl, "glXGetClientString");
    XVisualInfo *(*ChooseVisual)(Display *, int, int *) = dlsym(gl, "glXChooseVisual");
    GLXContext (*CreateContext)(Display *, XVisualInfo *, GLXContext, Bool) = dlsym(gl, "glXCreateContext");
    Bool (*MakeCurrent)(Display *, Window, GLXContext) = dlsym(gl, "glXMakeCurrent");
    const unsigned char *(*GetString)(unsigned int) = dlsym(gl, "glGetString");
    if (!QueryServerString || !ChooseVisual || !CreateContext || !MakeCurrent || !GetString) {
        printf("glxcheck: FAIL missing GLX entry points\n");
        return 1;
    }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { printf("glxcheck: FAIL no X display\n"); return 1; }
    int ev, err;
    printf("glxcheck: X server has GLX extension: %s\n", XQueryExtension(dpy, "GLX", &ev, &ev, &err) ? "yes" : "NO");
    int scr = DefaultScreen(dpy);
    printf("glxcheck: server GLX vendor \"%s\", version \"%s\"; client vendor \"%s\"\n",
           QueryServerString(dpy, scr, GLX_VENDOR), QueryServerString(dpy, scr, GLX_VERSION),
           GetClientString ? GetClientString(dpy, GLX_VENDOR) : "?");

    int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16, 0 };
    XVisualInfo *vi = ChooseVisual(dpy, scr, attr);
    if (!vi) { printf("glxcheck: FAIL glXChooseVisual\n"); return 1; }
    XSetWindowAttributes swa = {0};
    swa.colormap = XCreateColormap(dpy, RootWindow(dpy, scr), vi->visual, AllocNone);
    Window win = XCreateWindow(dpy, RootWindow(dpy, scr), 0, 0, 64, 64, 0, vi->depth, InputOutput,
                               vi->visual, CWColormap, &swa);
    GLXContext ctx = CreateContext(dpy, vi, NULL, True);
    if (!ctx) { printf("glxcheck: FAIL glXCreateContext\n"); return 1; }
    if (!MakeCurrent(dpy, win, ctx)) { printf("glxcheck: FAIL glXMakeCurrent\n"); return 1; }
    printf("glxcheck: OK vendor \"%s\", renderer \"%s\", version \"%s\"\n",
           GetString(GL_VENDOR), GetString(GL_RENDERER), GetString(GL_VERSION));
    return 0;
}
