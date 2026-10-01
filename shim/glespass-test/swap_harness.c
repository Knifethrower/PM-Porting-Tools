#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
int main(void) {
    void *(*gpa)(const char *) = dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    void (*sw)(void *, unsigned long) = gpa("glXSwapBuffers");
    for (int i = 0; i < 100; i++) sw((void *)1, 0x42);
    puts("done");
    return 0;
}
