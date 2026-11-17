/*
 * lkpi_host.c - host backend for the Linux-API shim.
 *
 * Runs nvkm as an ordinary user process. This file is the part common to
 * both hardware sides: hw_mock.c (memory standing in for the card, for
 * tests) and hw_vfio.c (a real card through Linux VFIO). Locks are plain
 * atomics and waits poll, which is all a single-threaded harness needs;
 * nothing here is used on Windows.
 */
#include "lkpi_host.h"
#include "host_os.h"

/* ------------------------------------------------------------------ locks */
void lkpi_spin_lock(spinlock_t *lock)
{
    while (__atomic_exchange_n(&lock->opaque[0], 1, __ATOMIC_ACQUIRE))
        __builtin_ia32_pause();
}
void lkpi_spin_unlock(spinlock_t *lock) { __atomic_store_n(&lock->opaque[0], 0, __ATOMIC_RELEASE); }
unsigned long lkpi_spin_lock_irqsave(spinlock_t *lock) { lkpi_spin_lock(lock); return 0; }
void lkpi_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{ (void)flags; lkpi_spin_unlock(lock); }

void lkpi_mutex_init(struct mutex *m) { m->ready = 1; m->opaque[0] = 0; }
bool lkpi_mutex_trylock(struct mutex *m)
{ return !__atomic_exchange_n(&m->opaque[0], 1, __ATOMIC_ACQUIRE); }
void lkpi_mutex_lock(struct mutex *m)
{
    u64 start = host_time_ns();
    while (!lkpi_mutex_trylock(m)) {
        /* nvkm mutexes are not recursive; a harness that waits this long on
         * one has self-deadlocked, and a hang would hide where. */
        if (host_time_ns() - start > 20ULL * 1000000000ULL)
            lkpi_panic("mutex held for 20s: deadlock");
        host_sleep_us(50);
    }
}
void lkpi_mutex_unlock(struct mutex *m) { __atomic_store_n(&m->opaque[0], 0, __ATOMIC_RELEASE); }
bool lkpi_mutex_is_locked(struct mutex *m) { return __atomic_load_n(&m->opaque[0], __ATOMIC_ACQUIRE) != 0; }

/* ------------------------------------------------------------ time, waits */
bool lkpi_host_fast_time;
static u64 g_time_skew;

u64 lkpi_time_ns(void) { return host_time_ns() + __atomic_load_n(&g_time_skew, __ATOMIC_RELAXED); }
static void elapse(u64 ns)
{
    if (lkpi_host_fast_time) __atomic_fetch_add(&g_time_skew, ns, __ATOMIC_RELAXED);
    else host_sleep_us((unsigned int)(ns / 1000) + 1);
}
void lkpi_udelay(unsigned long usec) { elapse((u64)usec * 1000); }
/* Sleeping is the one place interrupts can be taken: nvkm never sleeps with
 * a spinlock held, so the hardware side may run its handler from here. */
void lkpi_msleep(unsigned int msec) { lkpi_host_hw_idle(); elapse((u64)msec * 1000000); }
void lkpi_event_init(struct lkpi_event *e) { e->opaque[0] = 0; }
void lkpi_event_signal(struct lkpi_event *e) { (void)e; }
void lkpi_event_wait(struct lkpi_event *e, unsigned int timeout_ms)
{
    (void)e;
    /* Waiters re-test their own condition; run queued work meanwhile, since
     * what they wait for is often completed by a work item. */
    lkpi_host_hw_idle();
    if (!lkpi_work_run_pending()) elapse((u64)(timeout_ms ? 1 : 0) * 1000000);
}
void lkpi_work_kick(void) { }

/* ----------------------------------------------------------------- memory */
void *lkpi_alloc(size_t size, bool zero) { return host_alloc(size, zero, 16); }
void lkpi_free(void *p) { host_free(p); }

/* ---------------------------------------------------------------- logging */
int lkpi_host_verbosity = 4;
void (*lkpi_host_log_hook)(int level, const char *text);
static unsigned int g_log_count[8];

void lkpi_log(int level, const char *text)
{
    if (level < 0) level = 0;
    if (level > 7) level = 7;
    g_log_count[level]++;
    if (lkpi_host_log_hook) lkpi_host_log_hook(level, text);
    if (level <= lkpi_host_verbosity) host_write(text);
}
unsigned int lkpi_host_log_count(int level) { return g_log_count[level & 7]; }

void lkpi_panic(const char *text)
{
    host_write("lkpi panic: ");
    host_write(text);
    host_write("\n");
    host_abort();
}

void lkpi_host_log_reset(void)
{
    unsigned int i;
    for (i = 0; i < 8; i++) g_log_count[i] = 0;
}
