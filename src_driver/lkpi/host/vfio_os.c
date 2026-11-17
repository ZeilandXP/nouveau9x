#define _GNU_SOURCE
#include "vfio_os.h"
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/vfio.h>

static int g_container = -1, g_group = -1, g_device = -1, g_irqfd = -1;
static uint64_t g_cfg_offset, g_cfg_size;

static int fail(const char *what)
{
    fprintf(stderr, "vfio: %s: %s\n", what, strerror(errno));
    return -1;
}

void vfio_os_close(void)
{
    if (g_irqfd >= 0) close(g_irqfd);
    if (g_device >= 0) close(g_device);
    if (g_group >= 0) close(g_group);
    if (g_container >= 0) close(g_container);
    g_container = g_group = g_device = g_irqfd = -1;
}

int vfio_os_open(const char *bdf)
{
    char path[PATH_MAX], link[PATH_MAX];
    struct vfio_group_status status = { .argsz = sizeof(status) };
    struct vfio_region_info cfg = { .argsz = sizeof(cfg), .index = VFIO_PCI_CONFIG_REGION_INDEX };
    ssize_t n;

    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/iommu_group", bdf);
    n = readlink(path, link, sizeof(link) - 1);
    if (n < 0) {
        fprintf(stderr, "vfio: %s has no IOMMU group (is the IOMMU enabled, and is "
                "the address right?): %s\n", bdf, strerror(errno));
        return -1;
    }
    link[n] = '\0';

    g_container = open("/dev/vfio/vfio", O_RDWR);
    if (g_container < 0) return fail("open /dev/vfio/vfio (is the vfio module loaded?)");
    if (ioctl(g_container, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
        fprintf(stderr, "vfio: unexpected VFIO API version\n");
        goto err;
    }
    if (!ioctl(g_container, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
        fprintf(stderr, "vfio: type-1 IOMMU not supported\n");
        goto err;
    }
    snprintf(path, sizeof(path), "/dev/vfio/%s", basename(link));
    g_group = open(path, O_RDWR);
    if (g_group < 0) {
        fprintf(stderr, "vfio: open %s: %s (bind the card to vfio-pci first)\n", path, strerror(errno));
        goto err;
    }
    if (ioctl(g_group, VFIO_GROUP_GET_STATUS, &status)) { fail("VFIO_GROUP_GET_STATUS"); goto err; }
    if (!(status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        fprintf(stderr, "vfio: group not viable: every device in the card's IOMMU group "
                "(including its HDMI audio function) must be bound to vfio-pci\n");
        goto err;
    }
    if (ioctl(g_group, VFIO_GROUP_SET_CONTAINER, &g_container)) { fail("VFIO_GROUP_SET_CONTAINER"); goto err; }
    if (ioctl(g_container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU)) { fail("VFIO_SET_IOMMU"); goto err; }
    g_device = ioctl(g_group, VFIO_GROUP_GET_DEVICE_FD, bdf);
    if (g_device < 0) { fail("VFIO_GROUP_GET_DEVICE_FD"); goto err; }
    if (ioctl(g_device, VFIO_DEVICE_GET_REGION_INFO, &cfg)) { fail("config region info"); goto err; }
    g_cfg_offset = cfg.offset;
    g_cfg_size = cfg.size;
    return 0;
err:
    vfio_os_close();
    return -1;
}

int vfio_os_bar(unsigned int index, void **map, uint64_t *size)
{
    struct vfio_region_info info = { .argsz = sizeof(info), .index = index };
    void *p;
    *map = NULL;
    *size = 0;
    if (index > VFIO_PCI_BAR5_REGION_INDEX) return -1;
    if (ioctl(g_device, VFIO_DEVICE_GET_REGION_INFO, &info)) return fail("BAR region info");
    if (!info.size) return 0;
    if (!(info.flags & VFIO_REGION_INFO_FLAG_MMAP)) return 0;     /* I/O port BAR */
    p = mmap(NULL, info.size, PROT_READ | PROT_WRITE, MAP_SHARED, g_device, (off_t)info.offset);
    if (p == MAP_FAILED) return fail("mmap BAR");
    *map = p;
    *size = info.size;
    return 0;
}

int vfio_os_cfg_read(unsigned int offset, void *buf, unsigned int len)
{
    if ((uint64_t)offset + len > g_cfg_size) return -1;
    return pread(g_device, buf, len, (off_t)(g_cfg_offset + offset)) == (ssize_t)len ? 0 : fail("config read");
}

int vfio_os_cfg_write(unsigned int offset, const void *buf, unsigned int len)
{
    if ((uint64_t)offset + len > g_cfg_size) return -1;
    return pwrite(g_device, buf, len, (off_t)(g_cfg_offset + offset)) == (ssize_t)len ? 0 : fail("config write");
}

void *vfio_os_dma_pool(uint64_t size, uint64_t iova)
{
    struct vfio_iommu_type1_dma_map map = { .argsz = sizeof(map) };
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { fail("mmap DMA pool"); return NULL; }
    memset(p, 0, size);                 /* fault every page in before pinning */
    map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
    map.vaddr = (uintptr_t)p;
    map.iova = iova;
    map.size = size;
    if (ioctl(g_container, VFIO_IOMMU_MAP_DMA, &map)) {
        fail("VFIO_IOMMU_MAP_DMA (check 'ulimit -l' / RLIMIT_MEMLOCK, or run as root)");
        munmap(p, size);
        return NULL;
    }
    return p;
}

static int irq_set(uint32_t flags, int *fd)
{
    char buf[sizeof(struct vfio_irq_set) + sizeof(int)];
    struct vfio_irq_set *set = (struct vfio_irq_set *)buf;
    memset(buf, 0, sizeof(buf));
    set->argsz = (uint32_t)(sizeof(*set) + (fd ? sizeof(int) : 0));
    set->flags = flags;
    set->index = VFIO_PCI_INTX_IRQ_INDEX;
    set->start = 0;
    set->count = 1;
    if (fd) memcpy(set->data, fd, sizeof(int));
    return ioctl(g_device, VFIO_DEVICE_SET_IRQS, set);
}

int vfio_os_irq_enable(void)
{
    g_irqfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (g_irqfd < 0) return fail("eventfd");
    if (irq_set(VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER, &g_irqfd))
        return fail("VFIO_DEVICE_SET_IRQS (INTx)");
    return 0;
}

int vfio_os_irq_pending(void)
{
    uint64_t count;
    if (g_irqfd < 0) return 0;
    return read(g_irqfd, &count, sizeof(count)) == (ssize_t)sizeof(count);
}

void vfio_os_irq_unmask(void)
{
    if (g_irqfd >= 0 && irq_set(VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_UNMASK, NULL))
        fail("unmask INTx");
}

int vfio_os_read_file(const char *path, void **data, size_t *size)
{
    struct stat st;
    void *buf;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || st.st_size <= 0 || !(buf = malloc((size_t)st.st_size))) { close(fd); return -1; }
    if (read(fd, buf, (size_t)st.st_size) != st.st_size) { free(buf); close(fd); return -1; }
    close(fd);
    *data = buf;
    *size = (size_t)st.st_size;
    return 0;
}
