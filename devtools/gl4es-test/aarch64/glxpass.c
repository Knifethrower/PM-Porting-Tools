// GLX pass-through for a gl4es built with NOX11: every glX* entry point forwards to crusty_glX*, which the
// ports' glxsdl (GLX on the firmware's SDL2) provides. Same exports as Westonpack's gl4es_glxpass build
// (40 glX functions, 39 forwarded; glXGetProcAddress goes through glXGetProcAddressARB like there).
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>

#define EXPORT __attribute__((visibility("default")))
#define FWD(ret, name, params, args) \
    extern ret crusty_##name params; \
    EXPORT ret name params { return crusty_##name args; }
#define FWDV(name, params, args) \
    extern void crusty_##name params; \
    EXPORT void name params { crusty_##name args; }

FWD(GLXFBConfig *, glXChooseFBConfig, (Display *d, int s, const int *a, int *n), (d, s, a, n))
FWD(XVisualInfo *, glXChooseVisual, (Display *d, int s, int *a), (d, s, a))
FWDV(glXCopyContext, (Display *d, GLXContext a, GLXContext b, unsigned long m), (d, a, b, m))
FWD(GLXContext, glXCreateContext, (Display *d, XVisualInfo *v, GLXContext s, Bool r), (d, v, s, r))
FWD(GLXPixmap, glXCreateGLXPixmap, (Display *d, XVisualInfo *v, Pixmap p), (d, v, p))
FWD(GLXContext, glXCreateNewContext, (Display *d, GLXFBConfig c, int t, GLXContext s, Bool r), (d, c, t, s, r))
FWD(GLXPbuffer, glXCreatePbuffer, (Display *d, GLXFBConfig c, const int *a), (d, c, a))
FWD(GLXPixmap, glXCreatePixmap, (Display *d, GLXFBConfig c, Pixmap p, const int *a), (d, c, p, a))
FWD(GLXWindow, glXCreateWindow, (Display *d, GLXFBConfig c, Window w, const int *a), (d, c, w, a))
FWDV(glXDestroyContext, (Display *d, GLXContext c), (d, c))
FWDV(glXDestroyGLXPixmap, (Display *d, GLXPixmap p), (d, p))
FWDV(glXDestroyPbuffer, (Display *d, GLXPbuffer p), (d, p))
FWDV(glXDestroyPixmap, (Display *d, GLXPixmap p), (d, p))
FWDV(glXDestroyWindow, (Display *d, GLXWindow w), (d, w))
FWD(const char *, glXGetClientString, (Display *d, int n), (d, n))
FWD(int, glXGetConfig, (Display *d, XVisualInfo *v, int a, int *val), (d, v, a, val))
FWD(GLXContext, glXGetCurrentContext, (void), ())
FWD(Display *, glXGetCurrentDisplay, (void), ())
FWD(GLXDrawable, glXGetCurrentDrawable, (void), ())
FWD(GLXDrawable, glXGetCurrentReadDrawable, (void), ())
FWD(int, glXGetFBConfigAttrib, (Display *d, GLXFBConfig c, int a, int *v), (d, c, a, v))
FWD(GLXFBConfig *, glXGetFBConfigs, (Display *d, int s, int *n), (d, s, n))
FWD(__GLXextFuncPtr, glXGetProcAddressARB, (const GLubyte *n), (n))
FWDV(glXGetSelectedEvent, (Display *d, GLXDrawable w, unsigned long *m), (d, w, m))
FWD(XVisualInfo *, glXGetVisualFromFBConfig, (Display *d, GLXFBConfig c), (d, c))
FWD(Bool, glXIsDirect, (Display *d, GLXContext c), (d, c))
FWD(Bool, glXMakeContextCurrent, (Display *d, GLXDrawable w, GLXDrawable r, GLXContext c), (d, w, r, c))
FWD(Bool, glXMakeCurrent, (Display *d, GLXDrawable w, GLXContext c), (d, w, c))
FWD(int, glXQueryContext, (Display *d, GLXContext c, int a, int *v), (d, c, a, v))
FWDV(glXQueryDrawable, (Display *d, GLXDrawable w, int a, unsigned int *v), (d, w, a, v))
FWD(Bool, glXQueryExtension, (Display *d, int *e, int *v), (d, e, v))
FWD(const char *, glXQueryExtensionsString, (Display *d, int s), (d, s))
FWD(const char *, glXQueryServerString, (Display *d, int s, int n), (d, s, n))
FWD(Bool, glXQueryVersion, (Display *d, int *ma, int *mi), (d, ma, mi))
FWDV(glXSelectEvent, (Display *d, GLXDrawable w, unsigned long m), (d, w, m))
FWDV(glXSwapBuffers, (Display *d, GLXDrawable w), (d, w))
FWDV(glXUseXFont, (Font f, int a, int b, int c), (f, a, b, c))
FWDV(glXWaitGL, (void), ())
FWDV(glXWaitX, (void), ())
EXPORT __GLXextFuncPtr glXGetProcAddress(const GLubyte *n) { return glXGetProcAddressARB(n); }
