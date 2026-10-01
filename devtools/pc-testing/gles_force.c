/* x86_64 LD_PRELOAD for PC testing: every GLX context Unity creates becomes an OpenGL ES 3.2
 * context (Mesa: GLX_EXT_create_context_es2_profile), like crusty_glx gives it on the device.
 * Unity looks GL functions up through dlsym / glXGetProcAddressARB, so both are hooked. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef void *(*gpa_t)(const unsigned char *);
typedef void *(*cca_t)(void *, void *, void *, int, const int *);
static void *(*real_dlsym)(void *, const char *);
static gpa_t real_gpa;
static cca_t real_cca;

static void init(void) {
    if (!real_dlsym) real_dlsym = dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.34");
    if (!real_dlsym) real_dlsym = dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
}

static void *my_cca(void *dpy, void *cfg, void *share, int direct, const int *attr) {
    int a[64], n = 0;
    for (int i = 0; attr && attr[i] && n < 56; i += 2) {
        if (attr[i] == 0x2091 || attr[i] == 0x2092 || attr[i] == 0x9126 || attr[i] == 0x2094) continue;
        a[n++] = attr[i]; a[n++] = attr[i + 1];
    }
    a[n++] = 0x2091; a[n++] = 3;          /* major */
    a[n++] = 0x2092; a[n++] = 2;          /* minor */
    a[n++] = 0x9126; a[n++] = 0x4;        /* GLX_CONTEXT_ES2_PROFILE_BIT_EXT */
    a[n] = 0;
    void *c = real_cca(dpy, cfg, share, direct, a);
    fprintf(stderr, "[gles_force] glXCreateContextAttribsARB -> ES 3.2 context %p\n", c);
    return c;
}

static void *wrap(const char *name, void *p) {
    if (p && !strcmp(name, "glXCreateContextAttribsARB")) { real_cca = p; return (void *)my_cca; }
    return p;
}

static void *my_gpa(const unsigned char *name) { return wrap((const char *)name, real_gpa(name)); }

void *dlsym(void *h, const char *name) {
    init();
    void *p = real_dlsym(h, name);
    if (!name || !p) return p;
    if (!strcmp(name, "glXGetProcAddressARB") || !strcmp(name, "glXGetProcAddress")) { real_gpa = p; return (void *)my_gpa; }
    return wrap(name, p);
}
