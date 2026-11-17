/*
 * NVIDIA chipset identification from NV_PMC_BOOT_0.
 *
 * The decode mirrors nouveau (nvkm/engine/device/base.c): the chipset is
 * taken from the hardware register, never inferred from the PCI device ID.
 * This file is pure C with no OS dependency so it is host-testable.
 */
#ifndef NVX_CHIP_H
#define NVX_CHIP_H
#include <stdint.h>

typedef enum {
    NVX_FAMILY_UNKNOWN = 0,
    NVX_FAMILY_PASCAL,      /* NV130: GP100..GP108, the family this driver supports */
    NVX_FAMILY_OTHER        /* a recognised NVIDIA chipset outside NV130 */
} NVX_CHIP_FAMILY;

typedef struct {
    uint32_t Boot0;
    uint32_t Chipset;       /* e.g. 0x134 for GP104 */
    uint32_t Revision;      /* BOOT0[7:0] */
    NVX_CHIP_FAMILY Family;
    const char *Name;       /* "GP104", or "unknown" */
} NVX_CHIP_INFO;

/* Returns 1 when BOOT0 holds a decodable NV10+ chipset, 0 otherwise.
 * 'info' is always fully initialised. */
int NvxChipDecode(uint32_t boot0, NVX_CHIP_INFO *info);

/* Marketing name for a Pascal PCI device ID, or NULL when not listed. */
const char *NvxPascalPciName(uint16_t deviceId);

#endif
