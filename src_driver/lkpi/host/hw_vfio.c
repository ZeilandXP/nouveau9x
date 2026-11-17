/*
 * hw_vfio.c - hardware side of the host backend for a real card.
 *
 * The card is bound to vfio-pci on a Linux host. BARs are mmap()ed straight
 * from the VFIO device; DMA memory is a pool pinned and mapped through the
 * IOMMU at a fixed bus address; the line interrupt arrives on an eventfd
 * and is delivered whenever nvkm sleeps (see lkpi_host_hw_idle).
 */
#include "hw_vfio.h"
#include "host_os.h"
#include "vfio_os.h"

/* nvkm only needs BAR addresses to be distinct and to survive offset
 * arithmetic; ioremap() below translates them back to the mmap()ed BARs. */
#define BAR_FAKE_PHYS(i) ((phys_addr_t)((i) + 1) << 40)
#define DMA_IOVA_BASE 0x100000000ULL    /* 4 GiB: clear of the MSI window */

static struct { u8 *map; u64 size; } g_bar[6];
static u8 *g_pool;
static size_t g_pool_pages;
static unsigned long *g_pool_used;
static irq_handler_t g_irq_handler;
static void *g_irq_arg;
static bool g_in_irq;

u32 lkpi_vfio_rd32(u32 offset) { return ioread32(g_bar[0].map + offset); }

int lkpi_vfio_attach(const char *bdf, struct pci_dev *pdev, struct pci_bus *bus,
                     size_t dma_bytes)
{
    u8 cfg[0x40];
    u16 command;
    unsigned int i;

    if (vfio_os_open(bdf)) return -ENODEV;
    if (vfio_os_cfg_read(0, cfg, sizeof(cfg))) goto fail;
    memset(pdev, 0, sizeof(*pdev));
    memset(bus, 0, sizeof(*bus));
    pdev->bus = bus;
    bus->number = (unsigned char)simple_strtoul(bdf + 5, NULL, 16);
    bus->domain = (int)simple_strtoul(bdf, NULL, 16);
    pdev->devfn = PCI_DEVFN(simple_strtoul(bdf + 8, NULL, 16), simple_strtoul(bdf + 11, NULL, 16));
    pdev->vendor = get_unaligned_le16(cfg + 0x00);
    pdev->device = get_unaligned_le16(cfg + 0x02);
    pdev->revision = cfg[0x08];
    pdev->subsystem_vendor = get_unaligned_le16(cfg + 0x2c);
    pdev->subsystem_device = get_unaligned_le16(cfg + 0x2e);
    pdev->irq = cfg[0x3c];
    pdev->is_pcie = true;
    pdev->dev.init_name = bdf;

    for (i = 0; i < 6; i++) {
        void *map;
        uint64_t size;
        if (vfio_os_bar(i, &map, &size)) goto fail;
        g_bar[i].map = map;
        g_bar[i].size = map ? size : 0;
        pdev->resource[i].start = map ? BAR_FAKE_PHYS(i) : 0;
        pdev->resource[i].len = g_bar[i].size;
    }
    if (!g_bar[0].map) { lkpi_printk(3, "vfio: BAR0 is not mappable\n"); goto fail; }

    /* Memory decode and bus mastering; INTx left enabled. */
    command = get_unaligned_le16(cfg + 0x04);
    command |= 0x0006;
    if (vfio_os_cfg_write(0x04, &command, sizeof(command))) goto fail;

    g_pool_pages = PAGE_ALIGN(dma_bytes) >> PAGE_SHIFT;
    g_pool = vfio_os_dma_pool((uint64_t)g_pool_pages << PAGE_SHIFT, DMA_IOVA_BASE);
    g_pool_used = host_alloc(BITS_TO_LONGS(g_pool_pages) * sizeof(long), 1, 16);
    if (!g_pool || !g_pool_used) goto fail;
    if (vfio_os_irq_enable()) goto fail;
    return 0;
fail:
    lkpi_vfio_detach();
    return -EIO;
}

void lkpi_vfio_detach(void)
{
    vfio_os_close();        /* releases the BAR and DMA mappings with the fds */
    host_free(g_pool_used);
    g_pool_used = NULL;
    g_pool = NULL;
    g_pool_pages = 0;
    memset(g_bar, 0, sizeof(g_bar));
}

/* ------------------------------------------------------------------- MMIO */
void __iomem *ioremap(phys_addr_t offset, size_t size)
{
    unsigned int i;
    for (i = 0; i < 6; i++) {
        phys_addr_t base = BAR_FAKE_PHYS(i);
        if (g_bar[i].map && offset >= base && size <= g_bar[i].size &&
            offset - base <= g_bar[i].size - size)
            return g_bar[i].map + (offset - base);
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

/* -------------------------------------------------------------- DMA pool */
/* First-fit over a page bitmap. Everything nvkm hands the card (firmware
 * images, fault buffers, host-memory objects) comes from here, so every
 * address the card is given is one the IOMMU maps. */
static void *pool_alloc(size_t pages, dma_addr_t *bus)
{
    size_t start, run = 0;
    for (start = 0; start < g_pool_pages; start++) {
        run = test_bit((unsigned int)start, g_pool_used) ? 0 : run + 1;
        if (run == pages) {
            size_t first = start + 1 - pages, i;
            for (i = first; i <= start; i++) __set_bit((unsigned int)i, g_pool_used);
            memset(g_pool + (first << PAGE_SHIFT), 0, pages << PAGE_SHIFT);
            *bus = DMA_IOVA_BASE + ((dma_addr_t)first << PAGE_SHIFT);
            return g_pool + (first << PAGE_SHIFT);
        }
    }
    lkpi_printk(3, "vfio: DMA pool exhausted (%zu pages wanted); raise --dma-mb\n", pages);
    return NULL;
}

static void pool_free(void *cpu, size_t pages)
{
    size_t first = (size_t)((u8 *)cpu - g_pool) >> PAGE_SHIFT, i;
    for (i = 0; i < pages; i++) __clear_bit((unsigned int)(first + i), g_pool_used);
}

struct page *alloc_page(gfp_t gfp)
{
    struct page *page = host_alloc(sizeof(*page), 1, 16);
    (void)gfp;
    if (!page) return NULL;
    page->virt = pool_alloc(1, &page->phys);
    if (!page->virt) { host_free(page); return NULL; }
    return page;
}
void __free_page(struct page *page)
{
    if (page) { pool_free(page->virt, 1); host_free(page); }
}
void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, gfp_t gfp)
{
    (void)dev; (void)gfp;
    return pool_alloc(PAGE_ALIGN(size) >> PAGE_SHIFT, handle);
}
void dma_free_coherent(struct device *dev, size_t size, void *cpu, dma_addr_t handle)
{
    (void)dev; (void)handle;
    if (cpu) pool_free(cpu, PAGE_ALIGN(size) >> PAGE_SHIFT);
}
void *vmap(struct page **pages, unsigned int count, unsigned long flags, pgprot_t prot)
{ (void)pages; (void)count; (void)flags; (void)prot; return NULL; }
void vunmap(const void *addr) { (void)addr; }

/* ---------------------------------------------------------- PCI and IRQ */
int pci_read_config_dword(struct pci_dev *pdev, int where, u32 *val)
{
    (void)pdev;
    return vfio_os_cfg_read((unsigned int)where, val, 4) ? -EIO : 0;
}
int pci_write_config_byte(struct pci_dev *pdev, int where, u8 val)
{
    (void)pdev;
    return vfio_os_cfg_write((unsigned int)where, &val, 1) ? -EIO : 0;
}

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

/* Called whenever nvkm sleeps. VFIO masked the line when it fired; run the
 * handler and anything it queued, then let the line fire again. */
void lkpi_host_hw_idle(void)
{
    if (g_in_irq || !vfio_os_irq_pending()) return;
    g_in_irq = true;
    if (g_irq_handler) g_irq_handler(0, g_irq_arg);
    lkpi_work_run_pending();
    g_in_irq = false;
    vfio_os_irq_unmask();
}
