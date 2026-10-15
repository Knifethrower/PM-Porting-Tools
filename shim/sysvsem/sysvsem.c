/* sysvsem: System V semaphores in user space, for kernels built without CONFIG_SYSVIPC.
 *
 * The Unity 4 Linux player creates its thread semaphores with semget() and retries with the next
 * key forever when the call fails, so on a kernel without System V IPC (Knulli / muOS on the
 * Allwinner H700, e.g. RG Cube XX: semget returns ENOSYS) the game spins before it opens a window.
 * This library is preloaded into the emulated process (box64: BOX64_LD_PRELOAD) and overrides
 * semget / semop / semtimedop / semctl. It first tries the real call; if the kernel answers ENOSYS
 * (or SYSVSEM_FORCE=1 is set) it keeps the semaphore sets in this process instead: one mutex and
 * condition variable, counting values per semaphore, the operations the player uses (wait, post,
 * wait-for-zero, IPC_NOWAIT, GETVAL, SETVAL, GETALL, SETALL, IPC_RMID). Sets are visible only inside
 * this process, which is all the player needs. SEM_UNDO is ignored.
 *
 * Build (x86_64, on the PC):  gcc -O2 -shared -fPIC -o libsysvsem.so sysvsem.c -lpthread
 * License: 0BSD (do what you want, no warranty).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <time.h>

#define MAXSETS 256
#define MAXSEMS 64

struct set { int used, removed, nsems; key_t key; unsigned short val[MAXSEMS]; };

static struct set sets[MAXSETS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int mode;              /* 0 = undecided, 1 = kernel works, 2 = emulate */

static int (*real_semget)(key_t, int, int);
static int (*real_semop)(int, struct sembuf *, size_t);
static int (*real_semtimedop)(int, struct sembuf *, size_t, const struct timespec *);
static int (*real_semctl)(int, int, int, ...);

static void init(void)
{
    if (mode) return;
    real_semget = dlsym(RTLD_NEXT, "semget");
    real_semop = dlsym(RTLD_NEXT, "semop");
    real_semtimedop = dlsym(RTLD_NEXT, "semtimedop");
    real_semctl = dlsym(RTLD_NEXT, "semctl");
    const char *f = getenv("SYSVSEM_FORCE");
    if ((f && *f == '1') || !real_semget) { mode = 2; return; }
    errno = 0;
    int id = real_semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
    if (id < 0 && errno == ENOSYS) { mode = 2; return; }
    if (id >= 0 && real_semctl) real_semctl(id, 0, IPC_RMID);
    mode = 1;
}

static struct set *get(int id)
{
    if (id < 1 || id > MAXSETS || !sets[id - 1].used) { errno = EINVAL; return NULL; }
    if (sets[id - 1].removed) { errno = EIDRM; return NULL; }
    return &sets[id - 1];
}

int semget(key_t key, int nsems, int flags)
{
    init();
    if (mode == 1) return real_semget(key, nsems, flags);
    if (nsems < 0 || nsems > MAXSEMS) { errno = EINVAL; return -1; }
    pthread_mutex_lock(&lock);
    int i;
    if (key != IPC_PRIVATE)
        for (i = 0; i < MAXSETS; i++)
            if (sets[i].used && !sets[i].removed && sets[i].key == key) {
                if ((flags & IPC_CREAT) && (flags & IPC_EXCL)) { pthread_mutex_unlock(&lock); errno = EEXIST; return -1; }
                if (nsems > sets[i].nsems) { pthread_mutex_unlock(&lock); errno = EINVAL; return -1; }
                pthread_mutex_unlock(&lock);
                return i + 1;
            }
    if (key != IPC_PRIVATE && !(flags & IPC_CREAT)) { pthread_mutex_unlock(&lock); errno = ENOENT; return -1; }
    if (nsems == 0) { pthread_mutex_unlock(&lock); errno = EINVAL; return -1; }
    for (i = 0; i < MAXSETS && sets[i].used; i++) ;
    if (i == MAXSETS) { pthread_mutex_unlock(&lock); errno = ENOSPC; return -1; }
    memset(&sets[i], 0, sizeof sets[i]);
    sets[i].used = 1; sets[i].key = key; sets[i].nsems = nsems;
    pthread_mutex_unlock(&lock);
    return i + 1;
}

/* 1 = every operation can be applied now, 0 = must wait */
static int ready(struct set *s, struct sembuf *sops, size_t n)
{
    for (size_t k = 0; k < n; k++) {
        unsigned short v = s->val[sops[k].sem_num];
        if (sops[k].sem_op < 0 && v < (unsigned short)(-sops[k].sem_op)) return 0;
        if (sops[k].sem_op == 0 && v != 0) return 0;
    }
    return 1;
}

static int do_semop(int id, struct sembuf *sops, size_t n, const struct timespec *timeout)
{
    struct timespec deadline;
    if (timeout) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += timeout->tv_sec; deadline.tv_nsec += timeout->tv_nsec;
        if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }
    }
    pthread_mutex_lock(&lock);
    struct set *s = get(id);
    if (!s) { pthread_mutex_unlock(&lock); return -1; }
    for (size_t k = 0; k < n; k++)
        if (sops[k].sem_num >= s->nsems) { pthread_mutex_unlock(&lock); errno = EFBIG; return -1; }
    int nowait = 0;
    for (size_t k = 0; k < n; k++) if (sops[k].sem_flg & IPC_NOWAIT) nowait = 1;
    while (!ready(s, sops, n)) {
        if (nowait) { pthread_mutex_unlock(&lock); errno = EAGAIN; return -1; }
        int r = timeout ? pthread_cond_timedwait(&cond, &lock, &deadline) : pthread_cond_wait(&cond, &lock);
        if (s->removed) { pthread_mutex_unlock(&lock); errno = EIDRM; return -1; }
        if (r == ETIMEDOUT) { pthread_mutex_unlock(&lock); errno = EAGAIN; return -1; }
    }
    for (size_t k = 0; k < n; k++) s->val[sops[k].sem_num] += sops[k].sem_op;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    return 0;
}

int semop(int id, struct sembuf *sops, size_t n)
{
    init();
    if (mode == 1) return real_semop(id, sops, n);
    return do_semop(id, sops, n, NULL);
}

int semtimedop(int id, struct sembuf *sops, size_t n, const struct timespec *timeout)
{
    init();
    if (mode == 1) return real_semtimedop ? real_semtimedop(id, sops, n, timeout) : real_semop(id, sops, n);
    return do_semop(id, sops, n, timeout);
}

int semctl(int id, int semnum, int cmd, ...)
{
    union semun { int val; struct semid_ds *buf; unsigned short *array; } arg;
    va_list ap;
    va_start(ap, cmd);
    arg = va_arg(ap, union semun);
    va_end(ap);
    init();
    if (mode == 1) return real_semctl(id, semnum, cmd, arg);
    pthread_mutex_lock(&lock);
    struct set *s = get(id);
    if (!s) { pthread_mutex_unlock(&lock); return -1; }
    int r = 0;
    switch (cmd) {
    case GETVAL:
        if (semnum < 0 || semnum >= s->nsems) { errno = EINVAL; r = -1; } else r = s->val[semnum];
        break;
    case SETVAL:
        if (semnum < 0 || semnum >= s->nsems || arg.val < 0) { errno = EINVAL; r = -1; }
        else { s->val[semnum] = (unsigned short)arg.val; pthread_cond_broadcast(&cond); }
        break;
    case GETALL:
        for (int i = 0; i < s->nsems; i++) arg.array[i] = s->val[i];
        break;
    case SETALL:
        for (int i = 0; i < s->nsems; i++) s->val[i] = arg.array[i];
        pthread_cond_broadcast(&cond);
        break;
    case GETNCNT: case GETZCNT: case GETPID:
        r = 0;
        break;
    case IPC_STAT:
        if (arg.buf) { memset(arg.buf, 0, sizeof *arg.buf); arg.buf->sem_nsems = s->nsems; arg.buf->sem_perm.mode = 0600; }
        break;
    case IPC_SET:
        break;
    case IPC_RMID:
        s->removed = 1; s->used = 0;
        pthread_cond_broadcast(&cond);
        break;
    default:
        errno = EINVAL; r = -1;
    }
    pthread_mutex_unlock(&lock);
    return r;
}
