/* SDL2 GL bits: current window/context are per thread; SwapWindow refuses a window not current here */
#include <stdio.h>
#include <string.h>
extern int eglMakeCurrent(void *, void *, void *, void *);
extern int eglSwapBuffers(void *, void *);
static __thread void *cur_win, *cur_ctx;
static char err[128] = "";
void *SDL_GL_GetCurrentWindow(void) { return cur_win; }
void *SDL_GL_GetCurrentContext(void) { return cur_ctx; }
const char *SDL_GetError(void) { return err; }
int SDL_GL_MakeCurrent(void *w, void *c)
{
    if (w == cur_win && c == cur_ctx) return 0;
    if (!c) w = NULL;
    if (!eglMakeCurrent((void *)0xD0, c ? (void *)0x5A : NULL, c ? (void *)0x5A : NULL, c ? (void *)0xC0 : NULL)) {
        strcpy(err, "eglMakeCurrent failed"); return -1;
    }
    cur_win = w; cur_ctx = c; return 0;
}
void SDL_GL_SwapWindow(void *w)
{
    if (w != cur_win) { strcpy(err, "The specified window has not been made current"); return; }
    printf("SDL swap presented (window %p)\n", w);
}
int SDL_GL_SetSwapInterval(int i) { printf("SDL_GL_SetSwapInterval(%d)\n", i); return 0; }
