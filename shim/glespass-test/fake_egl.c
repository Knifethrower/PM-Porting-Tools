/* per-thread current context like real EGL; a context may be current on only one thread */
#include <stdio.h>
#include <pthread.h>
static __thread void *cur_ctx, *cur_draw, *cur_read;
static void *owner_ctx_thread;    /* ctx 0xC0 current somewhere? */
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static int err = 0x3000;
void *eglGetCurrentContext(void) { return cur_ctx; }
void *eglGetCurrentDisplay(void) { return cur_ctx ? (void *)0xD0 : NULL; }
void *eglGetCurrentSurface(int w) { return w == 0x3059 ? cur_draw : cur_read; }
int eglGetError(void) { int e = err; err = 0x3000; return e; }
int eglMakeCurrent(void *d, void *dr, void *rd, void *c)
{
    pthread_mutex_lock(&m);
    if (c && c != cur_ctx && owner_ctx_thread) { err = 0x3002; pthread_mutex_unlock(&m); return 0; } /* EGL_BAD_ACCESS */
    if (cur_ctx) owner_ctx_thread = NULL;
    cur_ctx = c; cur_draw = dr; cur_read = rd;
    if (c) owner_ctx_thread = (void *)1;
    pthread_mutex_unlock(&m);
    return 1;
}
void *eglGetProcAddress(const char *n) { return NULL; }
