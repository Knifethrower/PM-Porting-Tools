/* Minimal Unity-style Mono embedding: reproduces libmono startup under box64. */
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv) {
    const char *data = argc > 1 ? argv[1] : ".";
    char lib[1024], managed[1024], etc[1024];
    snprintf(lib, sizeof lib, "%s/Mono/x86_64/libmono.so", data);
    snprintf(managed, sizeof managed, "%s/Managed", data);
    snprintf(etc, sizeof etc, "%s/Mono/etc", data);
    void *m = dlopen(lib, RTLD_NOW);
    if (!m) { printf("dlopen: %s\n", dlerror()); return 1; }
    void (*set_dirs)(const char *, const char *) = dlsym(m, "mono_set_dirs");
    void (*config_parse)(const char *) = dlsym(m, "mono_config_parse");
    void *(*jit_init_version)(const char *, const char *) = dlsym(m, "mono_jit_init_version");
    void (*set_assemblies_path)(const char *) = dlsym(m, "mono_set_assemblies_path");
    set_assemblies_path(managed);
    set_dirs(managed, etc);
    config_parse(NULL);
    printf("calling mono_jit_init_version...\n"); fflush(stdout);
    void *domain = jit_init_version("Unity Root Domain", "v2.0.50727");
    printf("mono_jit_init_version OK, domain=%p\n", domain);
    return 0;
}
