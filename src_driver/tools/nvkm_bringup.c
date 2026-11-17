/*
 * nvkm_bringup.c - the probe / initialise / report / shut down sequence
 * shared by the VFIO runner and the host tests. It contains no hardware
 * access of its own: everything goes through nouveau.
 */
#include <core/pci.h>
#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/fb.h>
#include <subdev/therm.h>
#include <lkpi_backend.h>
#include "nvkm_bringup.h"

/* Internal to nouveau's device code; the nvif client path that normally
 * triggers them arrives with the display stage. */
int nvkm_device_init(struct nvkm_device *);
int nvkm_device_fini(struct nvkm_device *, bool suspend);

#define say(fmt, ...) lkpi_printk(0, fmt, ##__VA_ARGS__)

/* Firmware nouveau may request for an NV130 chip (see MODULE_FIRMWARE in
 * nvkm). Missing files are not an error: nouveau then runs without GR. */
static const char *const g_firmware_files[] = {
    "acr/bl.bin", "acr/ucode_load.bin", "acr/ucode_unload.bin", "acr/unload_bl.bin",
    "gr/fecs_bl.bin", "gr/fecs_inst.bin", "gr/fecs_data.bin", "gr/fecs_sig.bin",
    "gr/gpccs_bl.bin", "gr/gpccs_inst.bin", "gr/gpccs_data.bin", "gr/gpccs_sig.bin",
    "gr/sw_ctx.bin", "gr/sw_nonctx.bin", "gr/sw_bundle_init.bin", "gr/sw_method_init.bin",
    "nvdec/scrubber.bin",
    "sec2/desc.bin", "sec2/image.bin", "sec2/sig.bin",
    "sec2/desc-1.bin", "sec2/image-1.bin", "sec2/sig-1.bin",
};

unsigned int nvx_load_firmware(const char *root, const char *chip, nvx_read_file_fn read_file)
{
    unsigned int i, loaded = 0, compressed = 0;
    char lower[8];
    for (i = 0; chip[i] && i < sizeof(lower) - 1; i++) lower[i] = (char)tolower(chip[i]);
    lower[i] = '\0';
    for (i = 0; i < ARRAY_SIZE(g_firmware_files); i++) {
        char path[512], probe[520], *name;
        void *data;
        size_t size;
        snprintf(path, sizeof(path), "%s/nvidia/%s/%s", root, lower, g_firmware_files[i]);
        if (read_file(path, &data, &size)) {
            /* Distributions often ship linux-firmware compressed. */
            snprintf(probe, sizeof(probe), "%s.zst", path);
            if (!read_file(probe, &data, &size)) { lkpi_free(data); compressed++; }
            else {
                snprintf(probe, sizeof(probe), "%s.xz", path);
                if (!read_file(probe, &data, &size)) { lkpi_free(data); compressed++; }
            }
            continue;
        }
        /* nouveau asks by the name relative to the firmware root. */
        name = kstrdup(path + strlen(root) + 1, GFP_KERNEL);
        if (name && !lkpi_firmware_register(name, data, size)) loaded++;
    }
    say("firmware: %u of %u files loaded from %s/nvidia/%s\n", loaded,
        (unsigned int)ARRAY_SIZE(g_firmware_files), root, lower);
    if (compressed)
        say("firmware: %u files exist only compressed (.zst/.xz); decompress them "
            "into a directory and pass it with --firmware\n", compressed);
    if (loaded != ARRAY_SIZE(g_firmware_files))
        say("firmware: incomplete set; expect nouveau to run without GR (no 3D)\n");
    return loaded;
}

static const char *output_type(int type)
{
    switch (type) {
    case DCB_OUTPUT_ANALOG: return "analog";
    case DCB_OUTPUT_TV: return "TV";
    case DCB_OUTPUT_TMDS: return "TMDS (DVI/HDMI)";
    case DCB_OUTPUT_LVDS: return "LVDS";
    case DCB_OUTPUT_DP: return "DisplayPort";
    default: return "other";
    }
}

static void report(struct nvkm_device *device)
{
    struct nvkm_bios *bios = device->bios;
    u8 ver, hdr, cnt, len, i;

    say("---- summary ----\n");
    say("chipset : %s (0x%03x rev %02x)\n", device->chip->name, device->chipset, device->chiprev);
    if (bios)
        say("vbios   : %02x.%02x.%02x.%02x.%02x, %u bytes\n", bios->version.major,
            bios->version.chip, bios->version.minor, bios->version.micro,
            bios->version.patch, bios->size);
    if (device->fb && device->fb->ram)
        say("vram    : %llu MiB (nouveau ram type %d)\n", device->fb->ram->size >> 20,
            device->fb->ram->type);
    if (device->therm) {
        int temp = nvkm_therm_temp_get(device->therm);
        if (temp >= 0) say("temp    : %d C\n", temp);
        else say("temp    : unavailable (%d)\n", temp);
    }
    say("engines : gr %s, fifo %s, disp %s, ce0 %s\n", device->gr ? "yes" : "no (no firmware)",
        device->fifo ? "yes" : "no", device->disp ? "yes" : "no", device->ce[0] ? "yes" : "no");
    if (bios && dcb_table(bios, &ver, &hdr, &cnt, &len)) {
        say("outputs : DCB %x.%x, %u entries\n", ver >> 4, ver & 0xf, cnt);
        for (i = 0; i < cnt; i++) {
            struct dcb_output outp;
            u8 over, olen;
            if (!dcb_outp_parse(bios, i, &over, &olen, &outp)) continue;
            if (outp.type == DCB_OUTPUT_UNUSED || outp.type == DCB_OUTPUT_EOL) continue;
            say("  [%u] %-16s connector %u, heads %x, i2c %x, or %x, link %x\n", i,
                output_type(outp.type), outp.connector, outp.heads, outp.i2c_index,
                outp.or, outp.sorconf.link);
        }
    }
}

int nvx_bringup(struct pci_dev *pdev, const char *config, const char *debug, bool probe_only)
{
    struct nvkm_device *device = NULL;
    int ret = nvkm_device_pci_new(pdev, config, debug, true, true, ~0ULL, &device);
    say("nvkm_device_pci_new -> %d\n", ret);
    if (ret) {
        /* Past detection, nouveau expects the caller to tear down. */
        if (device && device->chip) nvkm_device_del(&device);
        return ret;
    }
    if (!probe_only) {
        ret = nvkm_device_init(device);
        say("nvkm_device_init -> %d\n", ret);
    }
    report(device);
    if (!probe_only && !ret) {
        /* Leaves the card as a clean shutdown would, including releasing
         * the secure-boot write-protected region when ACR was loaded. */
        int fini = nvkm_device_fini(device, false);
        say("nvkm_device_fini -> %d\n", fini);
    }
    nvkm_device_del(&device);
    return ret;
}
