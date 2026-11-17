#ifndef NVKM_BRINGUP_H
#define NVKM_BRINGUP_H
#include <lkpi.h>

/* Reads a whole file into memory from lkpi_alloc(); returns 0 or -1. */
typedef int (*nvx_read_file_fn)(const char *path, void **data, size_t *size);

/* Register the firmware nouveau may request for 'chip' ("GP104") from the
 * linux-firmware tree at 'root'. Returns the number of files found. */
unsigned int nvx_load_firmware(const char *root, const char *chip, nvx_read_file_fn read_file);

/* nouveau probe, optional device initialisation, a printed summary, and
 * shutdown. 'pdev' must be a chipset nouveau recognises. Returns nouveau's
 * error code from the first step that failed, or 0. */
int nvx_bringup(struct pci_dev *pdev, const char *config, const char *debug, bool probe_only);
#endif
