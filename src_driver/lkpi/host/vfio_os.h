/* C-library/Linux side of the VFIO hardware backend: every ioctl lives in
 * vfio_os.c, behind plain-C calls, so the lkpi side never sees libc. One
 * device at a time. Functions return 0 on success and -1 on failure, after
 * writing a description of the failure to stderr. */
#ifndef LKPI_VFIO_OS_H
#define LKPI_VFIO_OS_H
#include <stddef.h>
#include <stdint.h>
int vfio_os_open(const char *bdf);                 /* "0000:01:00.0" */
void vfio_os_close(void);
/* Map a BAR; a BAR that does not exist yields size 0 and success. */
int vfio_os_bar(unsigned int index, void **map, uint64_t *size);
int vfio_os_cfg_read(unsigned int offset, void *buf, unsigned int len);
int vfio_os_cfg_write(unsigned int offset, const void *buf, unsigned int len);
/* Allocate 'size' bytes of page-aligned, zeroed memory and make it visible
 * to the device at bus address 'iova' through the IOMMU. */
void *vfio_os_dma_pool(uint64_t size, uint64_t iova);
/* Legacy line interrupt through an eventfd. VFIO masks INTx on delivery;
 * unmask once the handler has run. */
int vfio_os_irq_enable(void);
int vfio_os_irq_pending(void);                     /* 1 if it fired since last call */
void vfio_os_irq_unmask(void);
/* Read a whole file; caller frees with host_free(). -1 if absent. */
int vfio_os_read_file(const char *path, void **data, size_t *size);
#endif
