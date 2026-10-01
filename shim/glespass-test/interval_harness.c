#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
int main(void) {
    void *(*gpa)(const char *) = dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    void (*si)(void *, unsigned long, int) = gpa("glXSwapIntervalEXT"); si((void *)1, 0x42, 1);
    void *gl = dlopen("libGL.so.1", RTLD_NOW);                      /* box64 path: dlsym on libGL.so.1 */
    int (*sm)(unsigned) = dlsym(gl, "glXSwapIntervalMESA"); sm(1);
    void (*sw)(void *, unsigned long) = gpa("glXSwapBuffers"); sw((void *)1, 0x42); sw((void *)1, 0x42);
    puts("done"); return 0;
}
