// LD_PRELOAD probe for PC testing (not part of the port).
// - Counts glBegin / glVertex3f / glCallList / glDrawArrays / glDrawElements per frame and the drawing time (from
//   the frame's glClear to the end of SDL_GL_SwapWindow), printed as averages every 300 frames.
//   The gl4es build calls libGL directly (symbol interposition below); the native GLES build gets
//   its entry points from SDL_GL_GetProcAddress, which is hooked to hand out counting wrappers.
// - FAKECLOCK=1: SDL_GetTicks advances exactly 16 ms per swapped frame, SDL_Delay does nothing
//   and the wall clock (CLOCK_REALTIME, time()) is frozen, so the game runs one logic step per frame with fixed random
//   seeds: two builds replaying the same replay draw identical frames.
// - KEYS="29:30-35 82:40-9000 ...": with FAKECLOCK, the keyboard state holds each SDL scancode
//   down over its frame range (Z 29, X 27, Up 82, Down 81, Left 80, Right 79, P 19, Esc 41).
// - DUMP=<dir> DUMPFRAMES="100 250 ...": writes those frames as <dir>/f<frame>.ppm (read back
//   before the swap). DUMPEVERY=N dumps every Nth frame as well.
// - KEYTIME=1: KEYS ranges count 16 ms units of real time since the first call instead of
//   drawn frames, so a slow device gets the same inputs at the same game time (no FAKECLOCK).
// gcc -O2 -shared -fPIC -o glcount.so glcount.c -ldl
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static long nbegin, ncall, ndraw, nelem, nvert, frames;
static double tsum, tmax, tclear;

#define NEXT(ret, name, args) static ret (*real_##name) args; \
  if (!real_##name) real_##name = (ret (*) args) dlsym(RTLD_NEXT, #name)

int clock_gettime(clockid_t id, struct timespec *ts) {
  NEXT(int, clock_gettime, (clockid_t, struct timespec *));
  if (id == CLOCK_REALTIME && getenv("FAKECLOCK")) {
    ts->tv_sec = 1700000000;
    ts->tv_nsec = 0;
    return 0;
  }
  return real_clock_gettime(id, ts);
}

time_t time(time_t *t) {
  NEXT(time_t, time, (time_t *));
  if (!getenv("FAKECLOCK"))
    return real_time(t);
  if (t) *t = 1700000000;
  return 1700000000;
}

static double now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

unsigned SDL_GetTicks(void) {
  NEXT(unsigned, SDL_GetTicks, (void));
  if (getenv("FAKECLOCK"))
    return (unsigned) (frames * 16);
  return real_SDL_GetTicks();
}

void SDL_Delay(unsigned ms) {
  NEXT(void, SDL_Delay, (unsigned));
  if (!getenv("FAKECLOCK"))
    real_SDL_Delay(ms);
}

const unsigned char *SDL_GetKeyboardState(int *n) {
  NEXT(const unsigned char *, SDL_GetKeyboardState, (int *));
  const char *keys = getenv("KEYS");
  if (!keys)
    return real_SDL_GetKeyboardState(n);
  static unsigned char state[512];
  memset(state, 0, sizeof state);
  long pos = frames;
  if (getenv("KEYTIME")) {
    static double keyStart;
    if (!keyStart) keyStart = now();
    pos = (long) ((now() - keyStart) * 1000 / 16);
  }
  const char *p = keys;
  int sc, from, to, len;
  while (sscanf(p, "%d:%d-%d%n", &sc, &from, &to, &len) == 3) {
    if (sc >= 0 && sc < 512 && pos >= from && pos <= to)
      state[sc] = 1;
    p += len;
  }
  if (n)
    *n = 512;
  return state;
}

// gl4es build: the game's PLT resolves these to the probe.
void glBegin(unsigned mode) { NEXT(void, glBegin, (unsigned)); nbegin++; real_glBegin(mode); }
void glVertex3f(float x, float y, float z) { NEXT(void, glVertex3f, (float, float, float)); nvert++; real_glVertex3f(x, y, z); }
void glCallList(unsigned l) { NEXT(void, glCallList, (unsigned)); ncall++; real_glCallList(l); }
void glDrawArrays(unsigned m, int f, int c) { NEXT(void, glDrawArrays, (unsigned, int, int)); ndraw++; real_glDrawArrays(m, f, c); }
void glDrawElements(unsigned m, int c, unsigned t, const void *i) { NEXT(void, glDrawElements, (unsigned, int, unsigned, const void *)); nelem++; real_glDrawElements(m, c, t, i); }
void glClear(unsigned m) { NEXT(void, glClear, (unsigned)); tclear = now(); real_glClear(m); }

// Native GLES build: entry points come from SDL_GL_GetProcAddress.
static void *(*real_SDL_GL_GetProcAddress)(const char *);
static void (*gles_glDrawArrays)(unsigned, int, int);
static void (*gles_glDrawElements)(unsigned, int, unsigned, const void *);
static void (*gles_glClear)(unsigned);
static void counting_glDrawArrays(unsigned m, int f, int c) { ndraw++; gles_glDrawArrays(m, f, c); }
static void counting_glDrawElements(unsigned m, int c, unsigned t, const void *i) { nelem++; gles_glDrawElements(m, c, t, i); }
static void counting_glClear(unsigned m) { tclear = now(); gles_glClear(m); }

static void *realProc(const char *name) {
  if (!real_SDL_GL_GetProcAddress)
    real_SDL_GL_GetProcAddress = (void *(*)(const char *)) dlsym(RTLD_NEXT, "SDL_GL_GetProcAddress");
  return real_SDL_GL_GetProcAddress(name);
}

void *SDL_GL_GetProcAddress(const char *name) {
  void *real = realProc(name);
  if (!real)
    return real;
  if (!strcmp(name, "glDrawArrays")) { gles_glDrawArrays = (void (*)(unsigned, int, int)) real; return (void *) counting_glDrawArrays; }
  if (!strcmp(name, "glDrawElements")) { gles_glDrawElements = (void (*)(unsigned, int, unsigned, const void *)) real; return (void *) counting_glDrawElements; }
  if (!strcmp(name, "glClear")) { gles_glClear = (void (*)(unsigned)) real; return (void *) counting_glClear; }
  return real;
}

static int wantDump(long frame) {
  const char *every = getenv("DUMPEVERY");
  if (every && atol(every) > 0 && frame % atol(every) == 0)
    return 1;
  const char *list = getenv("DUMPFRAMES");
  if (!list) return 0;
  char key[32];
  snprintf(key, sizeof key, " %ld ", frame);
  char buf[4096];
  snprintf(buf, sizeof buf, " %s ", list);
  return strstr(buf, key) != NULL;
}

static void dump(long frame) {
  const char *dir = getenv("DUMP");
  if (!dir || !wantDump(frame)) return;
  // Through SDL for both builds: the gl4es build's SDL hands out gl4es's functions, the native
  // build's SDL the GLES ones (SDL dlopens the GL library RTLD_LOCAL, so dlsym cannot see it).
  void (*readPixels)(int, int, int, int, unsigned, unsigned, void *) =
    (void (*)(int, int, int, int, unsigned, unsigned, void *)) realProc("glReadPixels");
  void (*getIntegerv)(unsigned, int *) = (void (*)(unsigned, int *)) realProc("glGetIntegerv");
  if (!readPixels || !getIntegerv) {
    fprintf(stderr, "GLCOUNT: cannot dump, no glReadPixels\n");
    return;
  }
  int vp[4];
  getIntegerv(0x0BA2 /* GL_VIEWPORT */, vp);
  int w = vp[0] * 2 + vp[2], h = vp[1] * 2 + vp[3];
  unsigned char *px = malloc((size_t) w * h * 4);
  // RGBA is the format every GLES implementation must support for glReadPixels.
  readPixels(0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, px);
  char buf[1024];
  snprintf(buf, sizeof buf, "%s/f%ld.ppm", dir, frame);
  FILE *f = fopen(buf, "wb");
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = h - 1; y >= 0; y--)
    for (int x = 0; x < w; x++)
      fwrite(px + ((size_t) y * w + x) * 4, 1, 3, f);
  fclose(f);
  free(px);
}

void SDL_GL_SwapWindow(void *w) {
  NEXT(void, SDL_GL_SwapWindow, (void *));
  dump(frames + 1);
  real_SDL_GL_SwapWindow(w);
  // EXITFRAME=N: leave after N drawn frames (device benchmarks); EXITSECONDS=S after S seconds.
  static double started;
  if (!started) started = now();
  const char *ef = getenv("EXITFRAME"), *es = getenv("EXITSECONDS");
  if ((ef && frames + 1 >= atol(ef)) || (es && now() - started >= atof(es))) {
    fprintf(stderr, "GLCOUNT: exit after %ld frames, %.1f s\n", frames + 1, now() - started);
    exit(0);
  }
  double d = now() - tclear; tsum += d; if (d > tmax) tmax = d;
  if (++frames % 300 == 0) {
    fprintf(stderr, "GLCOUNT per frame: glBegin %ld, glVertex3f %ld, glCallList %ld, glDrawArrays %ld, glDrawElements %ld; draw %.2f ms avg, %.2f max\n",
            nbegin / 300, nvert / 300, ncall / 300, ndraw / 300, nelem / 300, tsum / 300 * 1000, tmax * 1000);
    nbegin = ncall = ndraw = nelem = nvert = 0; tsum = tmax = 0;
  }
}
