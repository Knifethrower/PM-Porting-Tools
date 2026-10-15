// LD_PRELOAD crash reporter for devices without gdb: on SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT/SIGTRAP it
// prints the signal, fault address, a glibc backtrace and the executable mappings of /proc/self/maps
// to stderr (the launcher's log.txt), then re-raises. Turn the addresses into lines on the PC with
// the unstripped binary: addr2line -f -e game <address - mapping start + mapping offset>.
//   clang --target=aarch64-linux-gnu --sysroot=$HOME/chroot-bullseye -fuse-ld=lld -O2 -shared -fPIC \
//         -o segvtrace.so segvtrace.c
//   LD_PRELOAD=/tmp/segvtrace.so ./game
// License: 0BSD.
#define _GNU_SOURCE
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static void handler(int sig, siginfo_t *si, void *ctx) {
  char buf[512];
  int n = snprintf(buf, sizeof buf, "\n*** segvtrace: signal %d (%s), fault address %p\n", sig, strsignal(sig), si->si_addr);
  write(2, buf, n);
#if defined(__aarch64__)
  ucontext_t *uc = ctx;
  n = snprintf(buf, sizeof buf, "pc %p lr %p sp %p\n", (void *)uc->uc_mcontext.pc,
               (void *)uc->uc_mcontext.regs[30], (void *)uc->uc_mcontext.sp);
  write(2, buf, n);
#endif
  void *frames[64];
  int count = backtrace(frames, 64);
  backtrace_symbols_fd(frames, count, 2);
  write(2, "--- executable mappings ---\n", 28);
  int fd = open("/proc/self/maps", O_RDONLY);
  if (fd >= 0) {  // copy the r-xp lines only
    char line[512];
    int len = 0;
    char c;
    while (read(fd, &c, 1) == 1) {
      if (len < (int)sizeof line - 1) line[len++] = c;
      if (c == '\n') {
        line[len] = 0;
        if (strstr(line, " r-xp ")) write(2, line, len);
        len = 0;
      }
    }
    close(fd);
  }
  signal(sig, SIG_DFL);
  raise(sig);
}

__attribute__((constructor)) static void install(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = handler;
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
  for (unsigned i = 0; i < sizeof sigs / sizeof *sigs; i++) sigaction(sigs[i], &sa, NULL);
}
