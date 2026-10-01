#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
extern void *gl4es_GetProcAddress(const char *);
extern int SDL_GL_MakeCurrent(void *, void *);
extern void SDL_GL_SwapWindow(void *);
void *glXGetProcAddressARB(const char *n) { return gl4es_GetProcAddress(n); }
int glXQueryVersion(void *d, int *ma, int *mi) { *ma = 1; *mi = 4; return 1; }
void glViewport(int x, int y, int w, int h) { printf("crusty's hooked glViewport(%d,%d,%d,%d)\n", x, y, w, h); }
int glXMakeCurrent(void *d, unsigned long dr, void *c) { return 1; }            /* bookkeeping only, like crusty */
void glXSwapBuffers(void *d, unsigned long dr) { SDL_GL_SwapWindow((void *)0x77); }
__attribute__((constructor)) static void init(void) { SDL_GL_MakeCurrent((void *)0x77, (void *)0x55); }  /* main thread */
void glXSwapIntervalEXT(void *d, unsigned long dr, int i) { printf("crusty glXSwapIntervalEXT(%d)\n", i); }
int glXSwapIntervalMESA(unsigned i) { printf("crusty glXSwapIntervalMESA(%u)\n", i); return 0; }
