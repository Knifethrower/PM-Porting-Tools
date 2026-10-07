# sysvsem: System V semaphores in user space (x86_64 preload for box64 games)

## Problem
Some handheld kernels are built without `CONFIG_SYSVIPC` (Knulli and muOS on the Allwinner H700: RG Cube XX,
RG35XX-H/40XX family; checked 2026-09-28, `zcat /proc/config.gz | grep SYSVIPC`). The Unity 4 Linux player
creates its thread semaphores with `semget()` and, when the call fails, retries with the next key forever
(it only expects EEXIST). Symptom: the game prints its three `Mono path` lines and sits at 100% CPU on one
thread, no window, no sound; `strace -c -p <pid>` shows only `semget = ENOSYS`. Early Unity 5 players
probably behave the same.

## Fix
`libsysvsem.so` is an **x86_64** shared library preloaded into the emulated game with box64's
`BOX64_LD_PRELOAD`. It overrides `semget`, `semop`, `semtimedop` and `semctl`: it tries the real call
once and, if the kernel answers ENOSYS (or `SYSVSEM_FORCE=1` is set), keeps the semaphore sets inside the
process (one mutex + condition variable; counting values; wait, post, wait-for-zero, IPC_NOWAIT;
GETVAL/SETVAL/GETALL/SETALL/IPC_RMID; SEM_UNDO ignored). Sets are visible only inside the process, which is
all the player needs. On kernels that have System V IPC it does nothing.

A native (aarch64) `LD_PRELOAD` cannot do this: box64 resolves wrapped libc functions with `dlsym` on its own
libc handle, so only an x86 preload is seen by the game.

## Use in a port
1. Ship `libsysvsem.so` in `libs.x64/` and `LICENSE.sysvsem.txt` in `licenses/`; keep the source in
   `tools/sysvsem-src/` (the README's Compile section: `gcc -O2 -shared -fPIC -o libsysvsem.so sysvsem.c -lpthread`).
2. Add `BOX64_LD_PRELOAD=libsysvsem.so` to the box64 settings on the wrapped command line (found through
   `BOX64_LD_LIBRARY_PATH`, which already contains `libs.x64`).
3. Test on the PC with the real game: `LD_PRELOAD=./libsysvsem.so SYSVSEM_FORCE=1 ./Game.x86_64` must play
   normally; on the device the player gets past Mono init (input, GL init, `Initialize engine version`).

First used by Ittle Dew, verified on the RG Cube XX (Knulli).
License: 0BSD (LICENSE.txt).
