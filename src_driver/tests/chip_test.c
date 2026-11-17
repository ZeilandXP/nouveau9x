#include "nvx_chip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    NVX_CHIP_INFO c;
    /* GP104 (GTX 1070/1080) reports chipset 0x134 in BOOT0[28:20]. */
    assert(NvxChipDecode(0x134000a1, &c));
    assert(c.Chipset == 0x134 && c.Revision == 0xa1);
    assert(c.Family == NVX_FAMILY_PASCAL && !strcmp(c.Name, "GP104"));
    /* Every NV130 member nouveau knows. */
    static const struct { uint32_t boot0; const char *name; } pascal[] = {
        { 0x130000a1, "GP100" }, { 0x132000a1, "GP102" }, { 0x134000a1, "GP104" },
        { 0x136000a1, "GP106" }, { 0x137000a1, "GP107" }, { 0x138000a1, "GP108" },
    };
    for (size_t i = 0; i < sizeof(pascal) / sizeof(pascal[0]); i++) {
        assert(NvxChipDecode(pascal[i].boot0, &c));
        assert(c.Family == NVX_FAMILY_PASCAL && !strcmp(c.Name, pascal[i].name));
    }
    /* 0x13x values with no known chip are not claimed as Pascal. */
    assert(NvxChipDecode(0x135000a1, &c) && c.Family == NVX_FAMILY_OTHER);
    /* Neighbouring families decode but are not Pascal: GM200, GV100, GA104. */
    assert(NvxChipDecode(0x120000a1, &c) && c.Family == NVX_FAMILY_OTHER && c.Chipset == 0x120);
    assert(NvxChipDecode(0x140000a1, &c) && c.Family == NVX_FAMILY_OTHER);
    assert(NvxChipDecode(0xb74000a1, &c) && c.Family == NVX_FAMILY_OTHER && c.Chipset == 0x174);
    /* Dead bus, zero, and the NV04 layout are rejected. */
    assert(!NvxChipDecode(0xffffffff, &c) && c.Family == NVX_FAMILY_UNKNOWN);
    assert(!NvxChipDecode(0x00000000, &c));
    assert(!NvxChipDecode(0x20004000, &c));

    assert(!strcmp(NvxPascalPciName(0x1b81), "NVIDIA GeForce GTX 1070 (GP104)"));
    assert(strstr(NvxPascalPciName(0x1b06), "1080 Ti"));
    assert(NvxPascalPciName(0x2484) == NULL); /* RTX 3070 is not ours */
    puts("chip_test: ok");
    return 0;
}
