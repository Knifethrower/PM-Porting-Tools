// Sampling profiler for device test builds (not part of the port). Linked into the test binary;
// starts by itself. Every 2 ms of CPU time the process uses, the thread that is running gets
// SIGPROF and its program counter is appended to prof.bin (8 bytes: thread id in the top 16
// bits, address below). prof_maps.txt is a copy of /proc/self/maps, refreshed every 2,500
// samples and at exit, so that addresses in libraries loaded later (the GL driver) resolve.
// prof_info.txt has the main thread's id. pctest/prof_report.py turns these into a table.
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

static int fd = -1;
static volatile long count;

static void copy_maps(void) {
  int in = open("/proc/self/maps", O_RDONLY);
  int out = open("prof_maps.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  char b[4096];
  ssize_t r;
  if (in >= 0 && out >= 0)
    while ((r = read(in, b, sizeof b)) > 0)
      if (write(out, b, r) < 0)
        break;
  if (in >= 0) close(in);
  if (out >= 0) close(out);
}

static void handler(int sig, siginfo_t *si, void *ctx) {
  ucontext_t *uc = (ucontext_t *) ctx;
  uint64_t pc;
#if defined(__aarch64__)
  pc = uc->uc_mcontext.pc;
#elif defined(__x86_64__)
  pc = uc->uc_mcontext.gregs[REG_RIP];
#else
  pc = 0;
#endif
  uint64_t v = (pc & 0xFFFFFFFFFFFFull) | ((uint64_t) (syscall(SYS_gettid) & 0xFFFF) << 48);
  if (fd >= 0 && write(fd, &v, sizeof v) < 0)
    return;
  if (__sync_add_and_fetch(&count, 1) % 2500 == 0)
    copy_maps();
}

__attribute__((constructor)) static void prof_start(void) {
  fd = open("prof.bin", O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
  FILE *f = fopen("prof_info.txt", "w");
  if (f) {
    fprintf(f, "main_tid %ld\npid %d\n", (long) (syscall(SYS_gettid) & 0xFFFF), (int) getpid());
    fclose(f);
  }
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigaction(SIGPROF, &sa, NULL);
  struct itimerval it = {{0, 2000}, {0, 2000}};
  setitimer(ITIMER_PROF, &it, NULL);
}

__attribute__((destructor)) static void prof_stop(void) {
  struct itimerval it = {{0, 0}, {0, 0}};
  setitimer(ITIMER_PROF, &it, NULL);
  copy_maps();
  if (fd >= 0) close(fd);
}
