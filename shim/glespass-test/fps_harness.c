#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
    void *(*gpa)(const char *) = dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    void (*sw)(void *, unsigned long) = gpa("glXSwapBuffers");
    for (int i = 0; i < 70; i++) { usleep(20000); sw((void *)1, 0x42); }
    puts("done"); return 0;
}
