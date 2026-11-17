#include "nvx_chip.h"
#include <stddef.h>
#include "nvx_pascal_ids.h"

static const struct { uint32_t chipset; const char *name; } g_pascal[] = {
    { 0x130, "GP100" },
    { 0x132, "GP102" },
    { 0x134, "GP104" },
    { 0x136, "GP106" },
    { 0x137, "GP107" },
    { 0x138, "GP108" },
};

int NvxChipDecode(uint32_t boot0, NVX_CHIP_INFO *info)
{
    size_t i;
    info->Boot0 = boot0;
    info->Chipset = 0;
    info->Revision = 0;
    info->Family = NVX_FAMILY_UNKNOWN;
    info->Name = "unknown";

    /* An unmapped or powered-down BAR0 reads all-ones or zero. */
    if (boot0 == 0xffffffffU || boot0 == 0)
        return 0;
    /* NV10 and later keep the chipset in BOOT0[28:20]; NV04/NV05 use a
     * different layout and are of no interest here. */
    if ((boot0 & 0x1f000000U) == 0)
        return 0;

    info->Chipset = (boot0 & 0x1ff00000U) >> 20;
    info->Revision = boot0 & 0xffU;
    info->Family = NVX_FAMILY_OTHER;
    if ((info->Chipset & 0x1f0U) == 0x130U) {
        for (i = 0; i < sizeof(g_pascal) / sizeof(g_pascal[0]); i++) {
            if (g_pascal[i].chipset == info->Chipset) {
                info->Family = NVX_FAMILY_PASCAL;
                info->Name = g_pascal[i].name;
                break;
            }
        }
    }
    return 1;
}

const char *NvxPascalPciName(uint16_t deviceId)
{
    size_t i;
    for (i = 0; i < sizeof(g_NvxPascalIds) / sizeof(g_NvxPascalIds[0]); i++)
        if (g_NvxPascalIds[i].device == deviceId)
            return g_NvxPascalIds[i].name;
    return NULL;
}
