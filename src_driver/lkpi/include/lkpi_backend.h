/*
 * lkpi_backend.h - the shim's interface towards the code that hosts nvkm
 * (miniport, VxD, or test harness), as opposed to lkpi.h, which is the
 * interface towards nvkm itself.
 */
#ifndef LKPI_BACKEND_H
#define LKPI_BACKEND_H
#include <lkpi.h>

/* Deferred work. The backend is told when work is queued and must arrange
 * for lkpi_work_run_pending() to run in a context that may sleep. */
void lkpi_work_kick(void);
int lkpi_work_run_pending(void);        /* returns the number of items run */

/* Firmware images by linux-firmware name, e.g. "nvidia/gp104/acr/bl.bin".
 * 'name' and 'data' must stay valid; nothing is copied. */
int lkpi_firmware_register(const char *name, const void *data, size_t size);
void lkpi_firmware_clear(void);

#endif
