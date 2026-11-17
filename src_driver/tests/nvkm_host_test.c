/*
 * Runs nouveau's real device-detection path (nvkm_device_pci_new) on the host
 * against memory that stands in for a GTX 1070's BARs.
 */
#include <core/pci.h>
#include <core/subdev.h>
#include <subdev/bios.h>
#include "../lkpi/host/lkpi_host.h"

#define CHECK(cond) do { if (!(cond)) { \
    lkpi_printk(0, "nvkm_host_test: FAILED %s (line %d)\n", #cond, __LINE__); \
    lkpi_panic("test failure"); } } while (0)

#define BAR0_SIZE 0x1000000u            /* 16 MiB of registers */
#define BAR0_PHYS 0xf6000000ULL
#define BAR1_PHYS 0xe0000000ULL
#define BAR3_PHYS 0xf0000000ULL

static u32 *bar0;

static void make_gtx1070(struct pci_dev *pdev, struct pci_bus *bus)
{
    memset(pdev, 0, sizeof(*pdev));
    memset(bus, 0, sizeof(*bus));
    bus->number = 1;
    pdev->bus = bus;
    pdev->devfn = PCI_DEVFN(0, 0);
    pdev->vendor = 0x10de; pdev->device = 0x1b81;
    pdev->subsystem_vendor = 0x3842; pdev->subsystem_device = 0x6173;
    pdev->revision = 0xa1;
    pdev->is_pcie = true;
    pdev->dev.init_name = "0000:01:00.0";
    pdev->resource[0].start = BAR0_PHYS; pdev->resource[0].len = BAR0_SIZE;
    pdev->resource[1].start = BAR1_PHYS; pdev->resource[1].len = 0x10000000;
    pdev->resource[3].start = BAR3_PHYS; pdev->resource[3].len = 0x02000000;
}

/* A minimal but well-formed option ROM: PCI ROM header, PCIR structure, a
 * BIT table with only the version ('i') entry, and a zero checksum. It is
 * what nouveau's shadowing accepts; it describes no outputs, clocks or
 * scripts, so anything that needs those tables reports them missing. */
#define ROM_SIZE 0x2000u
static void make_vbios(u8 *rom)
{
    static const u8 bit_sig[5] = { 0xff, 0xb8, 'B', 'I', 'T' };
    static const u8 version[5] = { 0x00, 0x50, 0x04, 0x86, 0x70 };  /* 86.04.50.00.70 */
    u8 sum = 0;
    u32 i;
    memset(rom, 0, ROM_SIZE);
    rom[0] = 0x55; rom[1] = 0xaa;
    rom[0x18] = 0x40;                           /* -> PCIR */
    memcpy(rom + 0x40, "PCIR", 4);
    rom[0x44] = 0xde; rom[0x45] = 0x10;         /* vendor */
    rom[0x46] = 0x81; rom[0x47] = 0x1b;         /* device */
    rom[0x4a] = 0x18;                           /* PCIR length */
    rom[0x4f] = 0x03;                           /* class: display */
    rom[0x50] = ROM_SIZE / 512;                 /* image length, 512-byte units */
    rom[0x54] = 0x00;                           /* code type: x86 */
    rom[0x55] = 0x80;                           /* last image */
    memcpy(rom + 0x100, bit_sig, 5);
    rom[0x100 + 9] = 6;                         /* entry size */
    rom[0x100 + 10] = 1;                        /* entry count */
    rom[0x10c] = 'i'; rom[0x10d] = 2;           /* id, version */
    rom[0x10e] = 5;                             /* length */
    rom[0x110] = 0x00; rom[0x111] = 0x02;       /* -> 0x200 */
    memcpy(rom + 0x200, version, 5);
    for (i = 0; i < ROM_SIZE - 1; i++) sum = (u8)(sum + rom[i]);
    rom[ROM_SIZE - 1] = (u8)(0 - sum);
}

static unsigned int count_subdevs(struct nvkm_device *device)
{
    struct nvkm_subdev *subdev;
    unsigned int n = 0;
    list_for_each_entry(subdev, &device->subdev, head) n++;
    return n;
}

int main(void)
{
    struct pci_dev pdev;
    struct pci_bus bus;
    struct nvkm_device *device = NULL;
    int ret;

    bar0 = lkpi_alloc(BAR0_SIZE, true);
    CHECK(bar0);
    lkpi_host_add_region(BAR0_PHYS, BAR0_SIZE, bar0);
    lkpi_host_fast_time = true;
    lkpi_host_verbosity = 2;            /* quiet while a failure is expected */
    /* NULL option strings select nouveau's defaults; an empty debug string
     * would parse as "fatal only". */

    /* Protocol note, learned the hard way: nvkm_device_pci_new() must only
     * be called for a chipset nouveau recognises. If detection itself fails
     * the half-built device stays registered and nvkm_device_del() on it
     * dereferences uninitialised lists; upstream never makes that call
     * because its probe bails out first. The miniport therefore gates on
     * its own NV_PMC_BOOT_0 decode (nvx_chip.c) before involving nvkm, and
     * that case is covered by tests/chip_test.c rather than here. */

    /* 1. GP104 with no option ROM anywhere: detected, then refused because
     *    nouveau cannot find a VBIOS. Teardown after this failure is valid. */
    bar0[0] = 0x134000a1;                       /* NV_PMC_BOOT_0: GP104 rev a1 */
    make_gtx1070(&pdev, &bus);
    ret = nvkm_device_pci_new(&pdev, NULL, NULL, true, true, ~0ULL, &device);
    CHECK(ret == -EINVAL);
    CHECK(device && device->chipset == 0x134 && device->chiprev == 0xa1);
    CHECK(device->card_type == GP100);
    CHECK(device->chip && !strcmp(device->chip->name, "GP104"));
    nvkm_device_del(&device);

    /* 2. With a ROM visible through PROM (BAR0 + 0x300000), nouveau
     *    constructs every subdevice and engine in its GP104 table. */
    make_vbios((u8 *)bar0 + 0x300000);
    make_gtx1070(&pdev, &bus);
    lkpi_host_verbosity = 6;
    ret = nvkm_device_pci_new(&pdev, NULL, NULL, true, true, ~0ULL, &device);
    lkpi_printk(0, "nvkm_device_pci_new -> %d, %u subdevs\n", ret, device ? count_subdevs(device) : 0);
    CHECK(ret == 0);
    CHECK(device->chipset == 0x134 && !strcmp(device->chip->name, "GP104"));
    CHECK(device->bios && device->bios->version.major == 0x86 && device->bios->version.chip == 0x04);
    CHECK(device->bios->version.minor == 0x50 && device->bios->version.patch == 0x70);
    CHECK(device->acr && device->bar && device->bus && device->devinit && device->fault);
    CHECK(device->fb && device->fuse && device->gpio && device->i2c && device->imem);
    CHECK(device->ltc && device->mc && device->mmu && device->therm && device->pci);
    CHECK(device->pmu && device->privring && device->timer && device->top);
    CHECK(device->ce[0] && device->ce[1] && device->ce[2] && device->ce[3]);
    CHECK(device->disp && device->dma && device->fifo);
    /* No signed firmware was registered, so nouveau leaves GR out, exactly
     * as it does on a Linux system without linux-firmware installed. */
    CHECK(!device->gr);
    CHECK(device->nvdec[0] && device->nvenc[0] && device->nvenc[1] && device->sec2 && device->sw);
    /* Pascal has no GSP and no clock control in nouveau. */
    CHECK(!device->gsp && !device->clk && !device->volt);
    CHECK(count_subdevs(device) == 32);
    nvkm_device_del(&device);
    CHECK(device == NULL);

    lkpi_free(bar0);
    lkpi_printk(0, "nvkm_host_test: ok\n");
    return 0;
}
