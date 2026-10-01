#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
int main(void) { setvbuf(stdout, NULL, _IONBF, 0);
    void *crusty = dlopen("libcrusty.so", RTLD_NOW | RTLD_GLOBAL);   /* stands in for LD_PRELOAD */
    void *gl = dlopen("libGL.so.1", RTLD_NOW);
    if (!crusty || !gl) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    ((void (*)(unsigned))dlsym(gl, "glClear"))(0x4100);
    ((void (*)(double))dlsym(gl, "glClearDepth"))(1.0);          /* desktop-only -> no-op */
    printf("glGetString -> %s\n", ((const char *(*)(unsigned))dlsym(gl, "glGetString"))(0x1F02));
    void *(*gpa)(const char *) = dlsym(gl, "glXGetProcAddress");
    void (*cd)(float) = gpa("glClearDepthf"); cd(0.5f);
    void (*cd2)(float) = gpa("glClearDepthfOES"); cd2(0.25f);
    int ma = 0, mi = 0; ((int (*)(void *, int *, int *))dlsym(gl, "glXQueryVersion"))(0, &ma, &mi);
    printf("glXQueryVersion -> %d.%d\n", ma, mi);
    ((void (*)(unsigned))dlsym(gl, "glCompileShader"))(7);
    void (*lk)(unsigned) = gpa("glLinkProgram"); lk(9);
    ((void (*)(int, int, int, int))dlsym(gl, "glViewport"))(0, 0, 640, 480);
    void (*cs)(unsigned) = (void (*)(unsigned))dlsym(RTLD_DEFAULT, "glCompileShader"); cs(11);
    void *(*g)(const char *) = dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    Dl_info di; dladdr((void *)g, &di); printf("global glXGetProcAddressARB lives in %s\n", di.dli_fname);
    void (*c2)(unsigned) = g("glCompileShader"); c2(13);
    return 0;
}
