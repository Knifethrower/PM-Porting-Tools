/*
 * nitwprof: sample where every thread of a process is executing, using the kernel's perf events
 * (task-clock, per thread). Unlike ptrace it never stops the threads, so box64's and Mono's
 * signal handling is untouched.
 *
 * usage: nitwprof <pid> <seconds> <hz> <outdir> [render.tid file]
 * writes <outdir>/samples.txt   "tid ip count" (ip in hex; kernel ips included)
 *        <outdir>/threads.txt   "tid name"
 *        <outdir>/maps.txt      copy of /proc/<pid>/maps at the end
 *        <outdir>/summary.txt   sample totals per thread, lost records, errors
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define MAX_THREADS 256
#define RING_PAGES 16                       /* data pages per thread (power of two) */
#define HASH_BITS 20

struct thr { int tid, fd; void *ring; uint64_t samples, lost; };
static struct thr thr[MAX_THREADS];
static int nthr;
static long page;

struct ent { uint64_t ip; int tid; uint32_t count; };
static struct ent *tab;
static uint64_t tab_used, dropped;

static void count(int tid, uint64_t ip)
{
    uint64_t h = (ip * 0x9E3779B97F4A7C15ull) ^ (uint64_t)tid * 0xC2B2AE3D27D4EB4Full;
    size_t mask = (1u << HASH_BITS) - 1, i = (h >> 20) & mask;
    for (size_t n = 0; n <= mask; n++, i = (i + 1) & mask) {
        if (!tab[i].count) {
            if (tab_used > (mask * 3) / 4) { dropped++; return; }
            tab[i].ip = ip; tab[i].tid = tid; tab[i].count = 1; tab_used++;
            return;
        }
        if (tab[i].ip == ip && tab[i].tid == tid) { tab[i].count++; return; }
    }
}

static int exclude_kernel;

static int open_thread(int tid, int hz, FILE *err)
{
    for (int i = 0; i < nthr; i++)
        if (thr[i].tid == tid) return 0;
    if (nthr >= MAX_THREADS) return -1;
    struct perf_event_attr a;
    memset(&a, 0, sizeof a);
    a.size = sizeof a;
    a.type = PERF_TYPE_SOFTWARE;
    a.config = PERF_COUNT_SW_TASK_CLOCK;
    a.sample_period = 1000000000ull / hz;
    a.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID;
    a.disabled = 0;
    a.exclude_hv = 1;
    a.exclude_kernel = exclude_kernel;
    int fd = syscall(SYS_perf_event_open, &a, tid, -1, -1, 0);
    if (fd < 0) {
        fprintf(err, "perf_event_open(tid %d): %s\n", tid, strerror(errno));
        return -1;
    }
    void *ring = mmap(NULL, (1 + RING_PAGES) * page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ring == MAP_FAILED) {
        fprintf(err, "mmap(tid %d): %s\n", tid, strerror(errno));
        close(fd);
        return -1;
    }
    thr[nthr++] = (struct thr){tid, fd, ring, 0, 0};
    return 0;
}

static void scan_threads(int pid, int hz, FILE *err)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/task", pid);
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.')
            open_thread(atoi(e->d_name), hz, err);
    closedir(d);
}

static void drain(struct thr *t)
{
    struct perf_event_mmap_page *mp = t->ring;
    char *data = (char *)t->ring + page;
    uint64_t size = (uint64_t)RING_PAGES * page;
    uint64_t head = __atomic_load_n(&mp->data_head, __ATOMIC_ACQUIRE), tail = mp->data_tail;
    while (tail < head) {
        struct perf_event_header h;
        uint64_t off = tail % size;
        char rec[256];
        memcpy(&h, data + off, sizeof h);          /* headers are 8-byte aligned, never split */
        if (h.size < sizeof h || h.size > sizeof rec) break;
        for (unsigned i = 0; i < h.size; i++)
            rec[i] = data[(off + i) % size];
        if (h.type == PERF_RECORD_SAMPLE) {
            uint64_t ip; uint32_t pid, tid;
            memcpy(&ip, rec + 8, 8); memcpy(&pid, rec + 16, 4); memcpy(&tid, rec + 20, 4);
            count(tid, ip);
            t->samples++;
        } else if (h.type == PERF_RECORD_LOST) {
            uint64_t lost; memcpy(&lost, rec + 16, 8);
            t->lost += lost;
        }
        tail += h.size;
    }
    __atomic_store_n(&mp->data_tail, tail, __ATOMIC_RELEASE);
}

/* ---- state sampler: where the main and render threads are when they are NOT running ----
   Every 2 ms read /proc/<pid>/task/<tid>/{stat,syscall,wchan}. Blocked threads are counted by
   (syscall, futex address for futex waits, kernel wait function, user pc of the syscall), and the
   first TL_SECS seconds are kept as a timeline: R running, F futex, I ioctl, P poll/epoll/select,
   N sleep, Y yield, W read/write, o other syscall, - not in a syscall (preempted / page fault). */
#define ST_KEYS 512
#define TL_SECS 10
#define ST_US 2000
struct skey { char key[176]; uint32_t n; };
static struct skey st_tab[3][ST_KEYS];
static int st_used[3], st_tids[3], st_n, st_pid, st_secs;
static uint64_t st_total[3];
static char st_tl[3][TL_SECS * (1000000 / ST_US) + 1];
static const char *st_names[3] = {"main", "render", "mali-cmar-backe"};
/* GPU load from devfreq, every 100 ms */
static char gpu_load_path[256];
static char gpu_log[TL_SECS * 10 * 4 * 24];
static int gpu_log_len;
static int st_tl_len;

static int read_small(const char *path, char *buf, int size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, size - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = 0;
    return n;
}

static void st_count(int t, const char *key)
{
    for (int i = 0; i < st_used[t]; i++)
        if (!strcmp(st_tab[t][i].key, key)) { st_tab[t][i].n++; return; }
    if (st_used[t] < ST_KEYS) {
        snprintf(st_tab[t][st_used[t]].key, sizeof st_tab[t][0].key, "%s", key);
        st_tab[t][st_used[t]++].n = 1;
    }
}

static char st_sample(int t)
{
    char path[96], stat[512], sc[256], wchan[96], key[176];
    if (st_tids[t] <= 0) return '.';
    snprintf(path, sizeof path, "/proc/%d/task/%d/stat", st_pid, st_tids[t]);
    if (read_small(path, stat, sizeof stat) < 0) return 0;
    char *rp = strrchr(stat, ')');
    char state = rp && rp[1] ? rp[2] : '?';
    st_total[t]++;
    if (state == 'R') { st_count(t, "running"); return 'R'; }
    snprintf(path, sizeof path, "/proc/%d/task/%d/syscall", st_pid, st_tids[t]);
    if (read_small(path, sc, sizeof sc) < 0) strcpy(sc, "?");
    snprintf(path, sizeof path, "/proc/%d/task/%d/wchan", st_pid, st_tids[t]);
    if (read_small(path, wchan, sizeof wchan) < 0) strcpy(wchan, "?");
    wchan[strcspn(wchan, "\n")] = 0;
    long nr = -1;
    unsigned long long a[6] = {0}, sp = 0, pc = 0;
    if (sscanf(sc, "%ld 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx 0x%llx", &nr, &a[0], &a[1], &a[2], &a[3],
               &a[4], &a[5], &sp, &pc) < 1)
        nr = -1;
    char code;
    switch (nr) {                                   /* aarch64 syscall numbers */
    case 98: code = 'F'; break;                     /* futex */
    case 29: code = 'I'; break;                     /* ioctl */
    case 73: case 72: case 22: code = 'P'; break;   /* ppoll, pselect6, epoll_pwait */
    case 101: case 115: code = 'N'; break;          /* nanosleep, clock_nanosleep */
    case 124: code = 'Y'; break;                    /* sched_yield */
    case 63: case 64: case 65: case 66: code = 'W'; break;
    case -1: code = '-'; break;
    default: code = 'o';
    }
    if (nr == 98)
        snprintf(key, sizeof key, "%c futex uaddr=0x%llx op=%llu wchan=%s pc=0x%llx", state, a[0], a[1] & 0x7f, wchan, pc);
    else if (nr >= 0)
        snprintf(key, sizeof key, "%c syscall=%ld wchan=%s pc=0x%llx", state, nr, wchan, pc);
    else
        snprintf(key, sizeof key, "%c not-in-syscall wchan=%s", state, wchan);
    st_count(t, key);
    return code;
}

static void *st_thread(void *arg)
{
    (void)arg;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    long ticks = (long)st_secs * (1000000 / ST_US);
    for (long i = 0; i < ticks; i++) {
        if (gpu_load_path[0] && i % 50 == 0 && gpu_log_len < (int)sizeof gpu_log - 40) {
            char b[64];
            if (read_small(gpu_load_path, b, sizeof b) > 0) {
                b[strcspn(b, "\n")] = 0;
                gpu_log_len += snprintf(gpu_log + gpu_log_len, sizeof gpu_log - gpu_log_len, "%ld %s\n", i * ST_US / 1000, b);
            }
        }
        for (int t = 0; t < st_n; t++) {
            char c = st_sample(t);
            if (st_tl_len < (int)sizeof st_tl[0] - 1) st_tl[t][st_tl_len] = c ? c : '?';
        }
        if (st_tl_len < (int)sizeof st_tl[0] - 1) st_tl_len++;
        next.tv_nsec += ST_US * 1000;
        if (next.tv_nsec >= 1000000000) { next.tv_sec++; next.tv_nsec -= 1000000000; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        if (kill(st_pid, 0) != 0) break;
    }
    return NULL;
}

static int cmp_skey(const void *x, const void *y)
{
    const struct skey *a = x, *b = y;
    return (a->n < b->n) - (a->n > b->n);
}

static void st_write(const char *out)
{
    char path[512];
    snprintf(path, sizeof path, "%s/states.txt", out);
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int t = 0; t < st_n; t++) {
        qsort(st_tab[t], st_used[t], sizeof st_tab[t][0], cmp_skey);
        fprintf(f, "== tid %d (%s), %llu samples every %d us\n", st_tids[t], st_names[t],
                (unsigned long long)st_total[t], ST_US);
        for (int i = 0; i < st_used[t]; i++)
            fprintf(f, "%6.2f%% %7u  %s\n", 100.0 * st_tab[t][i].n / (st_total[t] ? st_total[t] : 1), st_tab[t][i].n,
                    st_tab[t][i].key);
    }
    fclose(f);
    snprintf(path, sizeof path, "%s/timeline.txt", out);
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# %d us per character, first %d s. R run, F futex, I ioctl, P poll, N sleep, Y yield, W read/write, "
               "o other syscall, - not in a syscall\n", ST_US, TL_SECS);
    for (int t = 0; t < st_n; t++) {
        st_tl[t][st_tl_len] = 0;
        fprintf(f, "%s %d\n%s\n", st_names[t], st_tids[t], st_tl[t]);
    }
    fclose(f);
    snprintf(path, sizeof path, "%s/gpuload.txt", out);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "# ms since sample start, %s\n%s", gpu_load_path[0] ? gpu_load_path : "(no devfreq load file)", gpu_log);
        fclose(f);
    }
}

static int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "r"), *out = fopen(to, "w");
    if (!in || !out) { if (in) fclose(in); if (out) fclose(out); return -1; }
    char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: %s <pid> <seconds> <hz> <outdir>\n", argv[0]);
        return 2;
    }
    int pid = atoi(argv[1]), secs = atoi(argv[2]), hz = atoi(argv[3]);
    const char *out = argv[4];
    char path[512];
    page = sysconf(_SC_PAGESIZE);
    tab = calloc((size_t)1 << HASH_BITS, sizeof *tab);
    snprintf(path, sizeof path, "%s/summary.txt", out);
    FILE *sum = fopen(path, "w");
    if (!sum || !tab) { perror("setup"); return 1; }
    FILE *para = fopen("/proc/sys/kernel/perf_event_paranoid", "r");
    int paranoid = -99;
    if (para) { if (fscanf(para, "%d", &paranoid) != 1) paranoid = -99; fclose(para); }
    exclude_kernel = paranoid >= 2 && geteuid() != 0;         /* non-root may only sample user space */
    fprintf(sum, "pid %d, %d s at %d Hz, perf_event_paranoid %d, euid %d, kernel samples %s\n", pid, secs, hz,
            paranoid, geteuid(), exclude_kernel ? "off" : "on");

    scan_threads(pid, hz, sum);
    if (!nthr) {
        fprintf(sum, "no threads could be sampled (kernel without perf events, or not root?)\n");
        fclose(sum);
        return 1;
    }
    /* state sampler for the main thread (tid == pid) and, if known, the render thread */
    st_pid = pid; st_secs = secs; st_tids[st_n++] = pid;
    if (argc > 5) {
        char buf[32];
        if (read_small(argv[5], buf, sizeof buf) > 0 && atoi(buf) > 0 && atoi(buf) != pid)
            st_tids[st_n++] = atoi(buf);
    }
    if (st_n < 2) st_tids[st_n++] = -1;                        /* keep slots: 0 main, 1 render, 2 mali */
    {   /* the Mali driver's job-submission thread */
        char tp[64]; snprintf(tp, sizeof tp, "/proc/%d/task", pid);
        DIR *dd = opendir(tp); struct dirent *e;
        while (dd && (e = readdir(dd))) {
            char cp[128], nm[64];
            snprintf(cp, sizeof cp, "/proc/%d/task/%s/comm", pid, e->d_name);
            if (e->d_name[0] != '.' && read_small(cp, nm, sizeof nm) > 0 && !strncmp(nm, "mali-cmar-back", 14)) {
                st_tids[st_n++] = atoi(e->d_name); break;
            }
        }
        if (dd) closedir(dd);
    }
    {   /* GPU devfreq load file */
        DIR *dd = opendir("/sys/class/devfreq"); struct dirent *e;
        while (dd && (e = readdir(dd)))
            if (strstr(e->d_name, "gpu")) {
                snprintf(gpu_load_path, sizeof gpu_load_path, "/sys/class/devfreq/%s/load", e->d_name);
                char b[64];
                if (read_small(gpu_load_path, b, sizeof b) <= 0) gpu_load_path[0] = 0;
                break;
            }
        if (dd) closedir(dd);
    }
    pthread_t stt;
    int st_ok = pthread_create(&stt, NULL, st_thread, NULL) == 0;
    fprintf(sum, "state sampler: main %d, render %d, mali %d, gpu load %s\n", pid, st_tids[1],
            st_n > 2 ? st_tids[2] : -1, gpu_load_path[0] ? gpu_load_path : "not available");

    struct timespec t0, now;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int last_scan = 0;
    for (;;) {
        usleep(20000);
        for (int i = 0; i < nthr; i++) drain(&thr[i]);
        clock_gettime(CLOCK_MONOTONIC, &now);
        int el = (int)(now.tv_sec - t0.tv_sec);
        if (el >= secs) break;
        if (el != last_scan) { last_scan = el; scan_threads(pid, hz, sum); }   /* new threads */
        if (kill(pid, 0) != 0) { fprintf(sum, "process exited after %d s\n", el); break; }
    }
    for (int i = 0; i < nthr; i++) drain(&thr[i]);
    if (st_ok) { pthread_join(stt, NULL); st_write(out); }

    snprintf(path, sizeof path, "%s/samples.txt", out);
    FILE *s = fopen(path, "w");
    for (size_t i = 0; s && i < ((size_t)1 << HASH_BITS); i++)
        if (tab[i].count) fprintf(s, "%d %llx %u\n", tab[i].tid, (unsigned long long)tab[i].ip, tab[i].count);
    if (s) fclose(s);
    snprintf(path, sizeof path, "%s/threads.txt", out);
    FILE *tf = fopen(path, "w");
    for (int i = 0; tf && i < nthr; i++) {
        char cp[64], name[64] = "?";
        snprintf(cp, sizeof cp, "/proc/%d/task/%d/comm", pid, thr[i].tid);
        FILE *c = fopen(cp, "r");
        if (c) { if (fgets(name, sizeof name, c)) name[strcspn(name, "\n")] = 0; fclose(c); }
        fprintf(tf, "%d %s\n", thr[i].tid, name);
        fprintf(sum, "tid %d (%s): %llu samples, %llu lost\n", thr[i].tid, name,
                (unsigned long long)thr[i].samples, (unsigned long long)thr[i].lost);
    }
    if (tf) fclose(tf);
    snprintf(path, sizeof path, "/proc/%d/maps", pid);
    char to[512];
    snprintf(to, sizeof to, "%s/maps.txt", out);
    if (copy_file(path, to)) fprintf(sum, "could not copy %s\n", path);
    fprintf(sum, "distinct (tid, ip) pairs %llu, dropped %llu\n", (unsigned long long)tab_used, (unsigned long long)dropped);
    fclose(sum);
    return 0;
}
