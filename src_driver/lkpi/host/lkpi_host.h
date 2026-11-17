/* Controls for the host (test harness) backend of the Linux-API shim. */
#ifndef LKPI_HOST_H
#define LKPI_HOST_H
#include <lkpi.h>
#include <lkpi_backend.h>

/* Implemented by the hardware side (hw_mock.c or hw_vfio.c): called
 * whenever nvkm sleeps, so pending device interrupts can be delivered. */
void lkpi_host_hw_idle(void);
void lkpi_host_log_reset(void);

/* ---- hw_mock.c only ---- */
/* Make [phys, phys+size) resolve to 'mem' for ioremap(). */
void lkpi_host_add_region(phys_addr_t phys, size_t size, void *mem);
void lkpi_host_reset(void);                 /* forget regions, IRQ, config space, counters */

/* PCI configuration space served to pci_read_config_dword(). */
extern u8 lkpi_host_pci_config[256];

/* The handler nvkm registered with request_irq(), or NULL. */
irqreturn_t lkpi_host_raise_irq(void);
bool lkpi_host_irq_requested(void);

/* Logging: messages at or below 'verbosity' (syslog level) reach stderr;
 * every message is counted and offered to the hook. */
extern int lkpi_host_verbosity;
extern void (*lkpi_host_log_hook)(int level, const char *text);
unsigned int lkpi_host_log_count(int level);
/* Advance the clock without sleeping, so timeouts in nvkm elapse instantly. */
extern bool lkpi_host_fast_time;
#endif
