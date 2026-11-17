# NVX Pascal: GTX 10-series display drivers for Windows 2000/XP (and 98SE/ME)

A fork of [xddm_nvidia](https://github.com/sanjayseshan/xddm_nvidia) retargeted
at NVIDIA **Pascal** GPUs (NV130 family: GP100, GP102, GP104, GP106, GP107,
GP108 — GeForce GTX 1050 through 1080 Ti, GT 1030).

The upstream project drives RTX cards by booting NVIDIA's GSP-RM firmware and
talking RPC to it. Pascal has no GSP, so this fork replaces that half with the
Linux **nouveau** kernel core (`nvkm`), compiled unmodified behind a small
Linux-API shim, and keeps the rest: the XDDM display driver, the miniport
skeleton, the Windows 98 driver pair, and the Mesa NVC0 OpenGL port.

Bring-up target: **GeForce GTX 1070 (GP104, `10de:1b81`)**, Windows XP x64
under VFIO passthrough first.

## Status

| Stage | What | State |
| :-- | :-- | :-- |
| 1 | Fork, strip GSP/OpenRM, Pascal identification, BIOS (VBE) scanout, CPU 2D | Builds and passes host tests; **not yet run on hardware** |
| 2 | Linux-API shim + nvkm host harness | In progress — see `notes/PASCAL_PORT.md` |
| 3 | nvkm subdevs inside the miniport (VBIOS, FB, MMU, I2C/AUX, therm) | Not started |
| 4 | Native display (EVO core channel `0x987d`), dual head | Not started |
| 5 | FIFO + copy-engine 2D (`0xc1b5`) | Not started |
| 6 | Secure boot (ACR/SEC2) + GR | Not started |
| 7 | 3D ABI on nvkm + Mesa NVC0 ICD (`GL_RENDERER = NV134`) | Not started |
| 8 | Windows 98 VxD and XP x86 parity, packaging | Not started |

What stage 1 gives you, once verified on a card: XP boots to a desktop at the
resolutions the card's video BIOS offers (32bpp), single head, with the
unmodified `nvxdisp.dll` drawing through a CPU implementation of the 2D ABI.
No acceleration, no 3D, no second monitor.

Known limits that will remain even when every stage is done: nouveau cannot
change clocks, voltage or fan speed on Pascal, so the card runs at its VBIOS
boot clocks and 3D will be slow.

## Layout

```
src_driver/      Video miniport (nvxmp.sys / nvxmp32.sys)
  nvxmp.c          VideoPort entry points, IOCTL dispatch
  nvx_chip.c       Chipset decode from NV_PMC_BOOT_0 (as nouveau does) + PCI names
  nvx_vbe.c        BIOS mode table, mode set with read-back, EDID over VBE/DDC
  nvx_swblt.c      CPU backend for the fill/copy/upload 2D ABI
  nvx_log.c        COM1 + ring + C:\WINDOWS\nvxmp.log diagnostics
  nvx_3d_abi.h     User/kernel 3D ABI shared with the display driver and Mesa
  lkpi/            Linux kernel API shim that nvkm is compiled against
  tools/           PCI-ID/INF generator, PE import audit
  tests/           Host tests (no Windows or GPU needed)
nouveau/         Pinned nouveau sources (fetched by script, not committed)
src_disp/        XDDM GDI display driver (nvxdisp.dll), unchanged from upstream
src_win98/       Windows 98 VxD + 16-bit display driver (still upstream/RTX; stage 8)
mesa_patches/    XP shims and NVC0 winsys for Mesa (still upstream/RTX; stage 7)
dist_full/       XP x64 package (nvx.inf generated)
dist32/          XP x86 package (nvx.inf generated)
notes/           PASCAL_PORT.md is current; the other files are upstream RTX history
```

## Build

On Debian/Ubuntu:

```sh
sudo apt-get install build-essential gcc-mingw-w64-x86-64 gcc-mingw-w64-i686 binutils-mingw-w64 pci.ids
```

```sh
make -C src_driver all      # nvxmp.sys (x64) and nvxmp32.sys (x86)
make -C src_driver check    # host tests + PE import audit
make -C src_disp nvxdisp.dll nvxdisp32.dll
```

Package for XP x64: copy `src_driver/nvxmp.sys` and `src_disp/nvxdisp.dll` next
to `dist_full/nvx.inf`. For XP x86: copy `nvxmp32.sys` as `nvxmp.sys` and
`nvxdisp32.dll` as `nvxdisp.dll` next to `dist32/nvx.inf`. Install through
Device Manager → Update Driver → Have Disk.

The miniport writes a diagnostic log to COM1 and `C:\WINDOWS\nvxmp.log`; the
first lines after `HwVidInitialize entered` show the decoded chipset
(`chipset=0x134 (GP104)` on a GTX 1070).

## Firmware

Stages 1–5 need no firmware. GR/3D (stage 6 onward) needs the signed `acr`,
`sec2`, `nvdec` and `gr` images from linux-firmware's `nvidia/gp104/`
(or the directory matching your chip). They are not in this repository.

## Credits

Everything upstream credits still applies: Sanjay Seshan's xddm_nvidia, the
WINVBE project, ReactOS, and above all the nouveau developers, whose driver
this port is built on.

## License

MIT, as upstream. nouveau's `nvkm` is MIT-licensed. NVIDIA firmware remains
NVIDIA's and is subject to its own licence.
