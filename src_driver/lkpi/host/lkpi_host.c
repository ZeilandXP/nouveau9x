/*
 * lkpi_host.c - host backend for the Linux-API shim.
 *
 * Runs nvkm as an ordinary user process against memory that stands in for
 * the card's BARs. Locks are plain atomics and waits poll, which is all a
 * test harness needs; nothing here is used on Windows.
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
void lkpi_msleep(unsigned int msec) { elapse((u64)msec * 1000000); }
void lkpi_event_init(struct lkpi_event *e) { e->opaque[0] = 0; }
void lkpi_event_signal(struct lkpi_event *e) { (void)e; }
void lkpi_event_wait(struct lkpi_event *e, unsigned int timeout_ms)
{
    (void)e;
    /* Waiters re-test their own condition; run queued work meanwhile, since
     * what they wait for is often completed by a work item. */
    if (!lkpi_work_run_pending()) elapse((u64)(timeout_ms ? 1 : 0) * 1000000);
}
void lkpi_work_kick(void) { }

/* ----------------------------------------------------------------- memory */
void *lkpi_alloc(size_t size, bool zero) { return host_alloc(size, zero, 16); }
void lkpi_free(void *p) { host_free(p); }

struct page *alloc_page(gfp_t gfp)
{
    struct page *page = host_alloc(sizeof(*page), 1, 16);
    (void)gfp;
    if (!page) return NULL;
    page->virt = host_alloc(PAGE_SIZE, 1, PAGE_SIZE);
    if (!page->virt) { host_free(page); return NULL; }
    /* No device reads this memory, so the "bus address" only has to be
     * unique and page-aligned. */
    page->phys = (dma_addr_t)(uintptr_t)page->virt;
    return page;
}
void __free_page(struct page *page)
{
    if (page) { host_free(page->virt); host_free(page); }
}
void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp)
{
    void *p = host_alloc(PAGE_ALIGN(size), 1, PAGE_SIZE);
    (void)dev; (void)gfp;
    if (p) *handle = (dma_addr_t)(uintptr_t)p;
    return p;
}
void dma_free_coherent(struct device *dev, size_t size, void *cpu, dma_addr_t handle)
{ (void)dev; (void)size; (void)handle; host_free(cpu); }

/* vmap() of pages that are not adjacent needs a real MMU; the harness has
 * no caller for it (only nouveau's Tegra instmem uses it). */
void *vmap(struct page **pages, unsigned int count, unsigned long flags, pgprot_t prot)
{ (void)pages; (void)count; (void)flags; (void)prot; return NULL; }
void vunmap(const void *addr) { (void)addr; }

/* ------------------------------------------------------------------- MMIO */
#define MAX_REGIONS 8
static struct { phys_addr_t phys; size_t size; u8 *mem; } g_regions[MAX_REGIONS];
static unsigned int g_region_count;

void lkpi_host_add_region(phys_addr_t phys, size_t size, void *mem)
{
    if (g_region_count == MAX_REGIONS) lkpi_panic("too many host MMIO regions");
    g_regions[g_region_count].phys = phys;
    g_regions[g_region_count].size = size;
    g_regions[g_region_count].mem = mem;
    g_region_count++;
}

void __iomem *ioremap(phys_addr_t offset, size_t size)
{
    unsigned int i;
    for (i = 0; i < g_region_count; i++) {
        if (offset >= g_regions[i].phys && size <= g_regions[i].size &&
            offset - g_regions[i].phys <= g_regions[i].size - size)
            return g_regions[i].mem + (offset - g_regions[i].phys);
    }
    return NULL;
}
void __iomem *ioremap_wc(phys_addr_t offset, size_t size) { return ioremap(offset, size); }
void iounmap(void __iomem *addr) { (void)addr; }

struct io_mapping *io_mapping_create_wc(phys_addr_t base, size_t size)
{
    struct io_mapping *map = host_alloc(sizeof(*map), 1, 16);
    if (!map) return NULL;
    map->base = base; map->size = size; map->iomem = ioremap(base, size);
    if (!map->iomem) { host_free(map); return NULL; }
    return map;
}
void io_mapping_free(struct io_mapping *map) { host_free(map); }

/* -------------------------------------------------------------------- PCI */
u8 lkpi_host_pci_config[256];
int pci_read_config_dword(struct pci_dev *pdev, int where, u32 *val)
{
    (void)pdev;
    if (where < 0 || where > 252) return -EINVAL;
    *val = get_unaligned_le32(&lkpi_host_pci_config[where]);
    return 0;
}
int pci_write_config_byte(struct pci_dev *pdev, int where, u8 val)
{
    (void)pdev;
    if (where < 0 || where > 255) return -EINVAL;
    lkpi_host_pci_config[where] = val;
    return 0;
}

static irq_handler_t g_irq_handler;
static void *g_irq_arg;
int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
                const char *name, void *arg)
{
    (void)irq; (void)flags; (void)name;
    if (g_irq_handler) return -EBUSY;
    g_irq_handler = handler; g_irq_arg = arg;
    return 0;
}
void free_irq(unsigned int irq, void *arg)
{ (void)irq; if (arg == g_irq_arg) { g_irq_handler = NULL; g_irq_arg = NULL; } }
bool lkpi_host_irq_requested(void) { return g_irq_handler != NULL; }
irqreturn_t lkpi_host_raise_irq(void)
{ return g_irq_handler ? g_irq_handler(0, g_irq_arg) : IRQ_NONE; }

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

void lkpi_host_reset(void)
{
    unsigned int i;
    g_region_count = 0;
    g_irq_handler = NULL; g_irq_arg = NULL;
    for (i = 0; i < 8; i++) g_log_count[i] = 0;
    for (i = 0; i < sizeof(lkpi_host_pci_config); i++) lkpi_host_pci_config[i] = 0;
    lkpi_firmware_clear();
}
