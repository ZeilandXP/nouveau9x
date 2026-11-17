/*
 * hw_mock.c - hardware side of the host backend for tests: memory stands in
 * for the BARs, "DMA" memory is ordinary heap, and the interrupt handler is
 * called only when the test asks.
 */
#include "lkpi_host.h"
#include "host_os.h"

void lkpi_host_hw_idle(void) { }

/* ----------------------------------------------------------------- memory */
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

void lkpi_host_reset(void)
{
    unsigned int i;
    g_region_count = 0;
    g_irq_handler = NULL; g_irq_arg = NULL;
    for (i = 0; i < sizeof(lkpi_host_pci_config); i++) lkpi_host_pci_config[i] = 0;
    lkpi_host_log_reset();
    lkpi_firmware_clear();
}
