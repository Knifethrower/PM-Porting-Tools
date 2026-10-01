/* libXcursor.so.1 stand-in for the X11 stub: the player creates cursors from images; nothing is ever shown. */
#include <X11/Xlib.h>
#include <X11/Xcursor/Xcursor.h>
#include <stdlib.h>

extern Cursor XCreateFontCursor(Display *, unsigned int);

XcursorImage *XcursorImageCreate(int width, int height)
{
    XcursorImage *i = calloc(1, sizeof *i);
    i->version = XCURSOR_IMAGE_VERSION; i->size = width > height ? width : height; i->width = width; i->height = height;
    i->pixels = calloc((size_t)width * height, sizeof(XcursorPixel));
    return i;
}
void XcursorImageDestroy(XcursorImage *i) { if (i) { free(i->pixels); free(i); } }
XcursorImages *XcursorImagesCreate(int n) { XcursorImages *s = calloc(1, sizeof *s); s->images = calloc(n, sizeof(XcursorImage *)); return s; }
void XcursorImagesDestroy(XcursorImages *s) { if (s) { for (int i = 0; i < s->nimage; i++) XcursorImageDestroy(s->images[i]); free(s->images); free(s); } }
XcursorCursors *XcursorCursorsCreate(Display *d, int n) { XcursorCursors *c = calloc(1, sizeof *c); c->dpy = d; c->cursors = calloc(n, sizeof(Cursor)); return c; }
void XcursorCursorsDestroy(XcursorCursors *c) { if (c) { free(c->cursors); free(c); } }
Cursor XcursorImageLoadCursor(Display *d, const XcursorImage *i) { return XCreateFontCursor(d, 0); }
Cursor XcursorImagesLoadCursor(Display *d, const XcursorImages *i) { return XCreateFontCursor(d, 0); }
XcursorCursors *XcursorImagesLoadCursors(Display *d, const XcursorImages *i) { XcursorCursors *c = XcursorCursorsCreate(d, i->nimage); c->ncursor = i->nimage; for (int k = 0; k < i->nimage; k++) c->cursors[k] = XCreateFontCursor(d, 0); return c; }
Cursor XcursorLibraryLoadCursor(Display *d, const char *name) { return XCreateFontCursor(d, 0); }
Cursor XcursorShapeLoadCursor(Display *d, unsigned int shape) { return XCreateFontCursor(d, shape); }
XcursorBool XcursorSupportsARGB(Display *d) { return 0; }
XcursorBool XcursorSetDefaultSize(Display *d, int size) { return 1; }
int XcursorGetDefaultSize(Display *d) { return 24; }
XcursorBool XcursorSetTheme(Display *d, const char *t) { return 1; }
char *XcursorGetTheme(Display *d) { return NULL; }
