/* LD_PRELOAD tracer for PC tests: logs fopen/fread/fseek/ftell/fclose on files whose path contains
 * STDIO_TRACE (default "values_tbl"). Test-only, not shipped. From Tummy Bonbons. License: 0BSD.
 * Build (x86_64 PC test): gcc -O2 -shared -fPIC -o stdio_trace.so stdio_trace.c -ldl
 * aarch64 (qemu/device): build in the bullseye chroot the same way. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *traced[16];

static int is_traced(FILE *f)
{
	for (int i = 0; i < 16; i++)
		if (traced[i] == f && f)
			return 1;
	return 0;
}

FILE *fopen(const char *path, const char *mode)
{
	static FILE *(*real)(const char *, const char *);
	if (!real)
		real = dlsym(RTLD_NEXT, "fopen");
	FILE *f = real(path, mode);
	const char *pat = getenv("STDIO_TRACE");
	if (path && strstr(path, pat ? pat : "values_tbl")) {
		fprintf(stderr, "TRACE fopen(\"%s\", \"%s\") = %p\n", path, mode, (void *)f);
		for (int i = 0; i < 16; i++)
			if (!traced[i]) { traced[i] = f; break; }
	}
	return f;
}

size_t fread(void *p, size_t sz, size_t n, FILE *f)
{
	static size_t (*real)(void *, size_t, size_t, FILE *);
	if (!real)
		real = dlsym(RTLD_NEXT, "fread");
	size_t r = real(p, sz, n, f);
	if (is_traced(f))
		fprintf(stderr, "TRACE fread(%p, %zu, %zu, %p) = %zu\n", p, sz, n, (void *)f, r);
	return r;
}

int fseek(FILE *f, long off, int wh)
{
	static int (*real)(FILE *, long, int);
	if (!real)
		real = dlsym(RTLD_NEXT, "fseek");
	int r = real(f, off, wh);
	if (is_traced(f))
		fprintf(stderr, "TRACE fseek(%p, %ld, %d) = %d\n", (void *)f, off, wh, r);
	return r;
}

int fseeko(FILE *f, off_t off, int wh)
{
	static int (*real)(FILE *, off_t, int);
	if (!real)
		real = dlsym(RTLD_NEXT, "fseeko");
	int r = real(f, off, wh);
	if (is_traced(f))
		fprintf(stderr, "TRACE fseeko(%p, %ld, %d) = %d\n", (void *)f, (long)off, wh, r);
	return r;
}

int fclose(FILE *f)
{
	static int (*real)(FILE *);
	if (!real)
		real = dlsym(RTLD_NEXT, "fclose");
	if (is_traced(f)) {
		fprintf(stderr, "TRACE fclose(%p)\n", (void *)f);
		for (int i = 0; i < 16; i++)
			if (traced[i] == f) traced[i] = NULL;
	}
	return real(f);
}
