/* Hardware side of the host backend for a real card through Linux VFIO. */
#ifndef LKPI_HW_VFIO_H
#define LKPI_HW_VFIO_H
#include "lkpi_host.h"

/* Open the card at PCI address 'bdf', fill 'pdev' from its configuration
 * space, map its BARs and set up 'dma_bytes' of device-visible memory. */
int lkpi_vfio_attach(const char *bdf, struct pci_dev *pdev, struct pci_bus *bus,
                     size_t dma_bytes);
void lkpi_vfio_detach(void);
/* Direct 32-bit read of a BAR0 register, for use before nvkm exists. */
u32 lkpi_vfio_rd32(u32 offset);
#endif
