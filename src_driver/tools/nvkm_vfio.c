/*
 * nvkm_vfio - run this port's nvkm build against a real card from Linux.
 *
 * The exact nouveau sources and Linux-API shim that go into the Windows
 * miniport are linked into an ordinary process and pointed at a GPU bound to
 * vfio-pci. It performs nouveau's probe and device initialisation, prints
 * what it found (VBIOS, VRAM, outputs, temperature), then shuts the device
 * down again. This is how chip bring-up is debugged without rebooting a
 * Windows guest for every change.
 *
 *   nvkm_vfio [options] <pci-address>            e.g. 0000:01:00.0
 *     --firmware DIR   linux-firmware root          (default /lib/firmware)
 *     --debug SPEC     nouveau debug string         (default "info")
 *     --config SPEC    nouveau config string        (e.g. "NvBios=PROM")
 *     --dma-mb N       device-visible memory in MiB (default 64)
 *     --probe-only     construct the device but do not initialise it
 *     --force          continue even if the chipset is not Pascal
 *
 * The card must not be driving a console: nouveau will take it over.
 */
#include "../lkpi/host/hw_vfio.h"
#include "../lkpi/host/host_os.h"
#include "../lkpi/host/vfio_os.h"
#include "../nvx_chip.h"
#include "nvkm_bringup.h"

#define say(fmt, ...) lkpi_printk(0, fmt, ##__VA_ARGS__)

/* Files come back from malloc(); the shim's allocator on the host is the
 * same heap, so nouveau-side code may free them with lkpi_free(). */
static int read_file(const char *path, void **data, size_t *size)
{ return vfio_os_read_file(path, data, size); }

static bool valid_bdf(const char *s)
{
    /* dddd:bb:ss.f */
    static const char shape[] = "hhhh:hh:hh.h";
    unsigned int i;
    for (i = 0; shape[i]; i++) {
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (shape[i] == 'h' ? !hex : c != shape[i]) return false;
    }
    return s[i] == '\0';
}

static int usage(void)
{
    say("usage: nvkm_vfio [--firmware DIR] [--debug SPEC] [--config SPEC] [--dma-mb N]\n"
        "                 [--probe-only] [--force] <dddd:bb:ss.f>\n");
    return 2;
}

int main(int argc, char **argv)
{
    static struct pci_dev pdev;
    static struct pci_bus bus;
    const char *firmware = "/lib/firmware", *debug = "info", *config = NULL, *bdf = NULL;
    size_t dma_mb = 64;
    bool probe_only = false, force = false;
    NVX_CHIP_INFO chip;
    int i, ret, status = 1;

    lkpi_host_verbosity = 7;
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i], *value = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(arg, "--probe-only")) probe_only = true;
        else if (!strcmp(arg, "--force")) force = true;
        else if (!strcmp(arg, "--firmware") && value) { firmware = value; i++; }
        else if (!strcmp(arg, "--debug") && value) { debug = value; i++; }
        else if (!strcmp(arg, "--config") && value) { config = value; i++; }
        else if (!strcmp(arg, "--dma-mb") && value) { dma_mb = simple_strtoul(value, NULL, 10); i++; }
        else if (arg[0] != '-' && !bdf) bdf = arg;
        else return usage();
    }
    if (!bdf || !valid_bdf(bdf) || dma_mb < 4 || dma_mb > 4096) return usage();

    ret = lkpi_vfio_attach(bdf, &pdev, &bus, dma_mb << 20);
    if (ret) { say("could not attach %s (%d)\n", bdf, ret); return 1; }
    say("attached %s: %04x:%04x subsystem %04x:%04x rev %02x\n", bdf, pdev.vendor, pdev.device,
        pdev.subsystem_vendor, pdev.subsystem_device, pdev.revision);
    for (i = 0; i < 6; i++)
        if (pdev.resource[i].len)
            say("  BAR%d: %llu MiB\n", i, pdev.resource[i].len >> 20);

    /* Identify the chip ourselves first: nouveau must not be handed a
     * device it cannot detect (see tests/nvkm_host_test.c). */
    if (pdev.vendor != 0x10de || !NvxChipDecode(lkpi_vfio_rd32(0), &chip)) {
        say("NV_PMC_BOOT_0 = %08x: not a responding NVIDIA chip (is the card powered, "
            "and not in D3?)\n", lkpi_vfio_rd32(0));
        goto out;
    }
    say("NV_PMC_BOOT_0 = %08x: chipset 0x%03x (%s)\n", chip.Boot0, chip.Chipset, chip.Name);
    if (chip.Family != NVX_FAMILY_PASCAL) {
        say("this is not a Pascal (NV130) chipset%s\n", force ? "; continuing (--force)" :
            "; pass --force to try anyway");
        if (!force) goto out;
    } else {
        nvx_load_firmware(firmware, chip.Name, read_file);
    }

    ret = nvx_bringup(&pdev, config, debug, probe_only);
    status = ret ? 1 : 0;
out:
    lkpi_vfio_detach();
    say("%s\n", status ? "FAILED" : "OK");
    return status;
}
