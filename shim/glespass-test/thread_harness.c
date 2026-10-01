#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
typedef void *(*gpa_t)(const char *);
static gpa_t gpa;
static void *(*cur)(void);
static void *render(void *arg) {
    int (*mc)(void *, unsigned long, void *) = gpa("glXMakeCurrent");
    printf("render before bind: egl ctx %p\n", cur());
    mc((void *)1, 0x42, (void *)0x1234);
    printf("render after bind:  egl ctx %p\n", cur());
    void (*clear)(unsigned) = gpa("glClear"); clear(0x4000); clear(0x4000); clear(0x4000);
    void (*sw)(void *, unsigned long) = gpa("glXSwapBuffers"); sw((void *)1, 0x42);
    mc((void *)1, 0, NULL);
    printf("render after release: egl ctx %p\n", cur());
    return NULL;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    gpa = dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    cur = dlsym(RTLD_DEFAULT, "eglGetCurrentContext");
    int (*emc)(void *, void *, void *, void *) = dlsym(RTLD_DEFAULT, "eglMakeCurrent");
    (void)emc;
    int (*mc)(void *, unsigned long, void *) = gpa("glXMakeCurrent");
    mc((void *)1, 0x42, (void *)0x1234);
    mc((void *)1, 0, NULL);            /* Unity main thread hands the context to the render thread */
    printf("main after release: egl ctx %p\n", cur());
    pthread_t t; pthread_create(&t, NULL, render, NULL); pthread_join(t, NULL);
    mc((void *)1, 0x42, (void *)0x1234);
    printf("main after rebind: egl ctx %p\n", cur());
    puts("done");
    return 0;
}
