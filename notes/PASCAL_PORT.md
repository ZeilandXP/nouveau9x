# Pascal port: status and working notes

Bring-up card: GeForce GTX 1070 (GP104, chipset `0x134`, `10de:1b81`).
Everything below has been built and host-tested only. **Nothing has run on a
real card or in a Windows guest yet**; the "to verify on hardware" lists are
the next actions.

## Where things stand

| Stage | State |
| :-- | :-- |
| 1. BIOS (VBE) scanout miniport | Code complete, host-tested. Needs a first boot in XP. |
| 2. nvkm + Linux-API shim | nvkm/nvif (600 files) build for host, Win x64, Win x86. Probe path runs on the host against a mock card. VFIO runner written, **untested** (no VFIO device in the build environment). |
| 3. nvkm inside the miniport | Not started: needs the Windows backend for the shim. |
| 4–8 | Not started. |

## Stage 1 — to verify on hardware

Build, package and install as in the top-level README, then check
`C:\WINDOWS\nvxmp.log` (or COM1):

1. `NV_PMC_BOOT_0=... chipset=0x134 (GP104)` appears.
2. `BIOS mode table entries:` is non-zero and lists 32bpp modes. If it is
   zero, XP falls back to VGA by design; the log says which VBE call failed.
3. The desktop appears and repaints correctly (window drags exercise the
   copy path, solid fills the fill path).
4. `write-combined scanout mapping refused` should *not* appear; if it does,
   drawing still works but slowly.

Known unknowns for this stage:

- **INT10 on XP x86.** Upstream bypassed VBE on 32-bit XP because INT10
  misbehaved with its RTX card. This fork tries VBE on both architectures;
  x64 goes through VideoPort's emulator and is the safer first target.
- **`videoprt.sys` patch.** `DriverEntry` still carries upstream's in-memory
  patch of `VpIsMultiFunctionSupported` (the GTX 1070 is also a
  display + audio multi-function device). It is keyed to byte signatures in
  upstream's XP builds; on a different service pack it logs a warning and
  the device may fail to start with Code 10.
- **Resolution list** is whatever the card's VBE BIOS offers; expect no
  1440p/4K until native modesetting (stage 4).

## Stage 2 — how the nvkm build works

- `tools/fetch_nouveau.sh` fetches `drivers/gpu/drm/nouveau` at Linux v6.6 by
  commit hash into `nouveau/` (not committed) and applies `nouveau_patches/`.
- `src_driver/lkpi/include/lkpi.h` stands in for every `<linux/...>` header.
  nvkm translation units are freestanding and see nothing else.
- `src_driver/lkpi/lkpi_string.c`, `lkpi_core.c`: printf, strings, red-black
  tree, work queue, firmware registry. Portable to all targets.
- OS services are the `lkpi_*` functions at the "backend" section of
  `lkpi.h`. Backends so far: `lkpi/host/` (user process; `hw_mock.c` for
  tests, `hw_vfio.c` for a real card).
- `make -C src_driver nvkm-all` builds `build/{host,w64,w32}/libnvkm.a`.
  `make -C src_driver check` runs every host test.

Things learned that the Windows backend must respect:

- **Call protocol.** Never hand nouveau a chip it cannot detect:
  `nvkm_device_pci_new()` leaves a half-registered device behind and
  `nvkm_device_del()` on it crashes. Gate on `NvxChipDecode()` first.
  After a failure *past* detection, `nvkm_device_del()` is required.
- **Option strings.** Pass `NULL`, not `""`, for nouveau's debug string; an
  empty string parses as "fatal only".
- **LLP64.** `long` is 32 bits on Windows x64. The build promotes
  pointer/integer size mismatches to errors; that caught three truncating
  casts, fixed in `nouveau_patches/0001`. Remaining `unsigned long` uses in
  nvkm were reviewed and hold register values, bitmaps or IRQ flags.
- **No firmware → no GR**, and nouveau carries on. That is the expected
  state until stage 6; the device then has 32 subdevices on GP104.
- **Interrupts** are only delivered while nvkm sleeps in the host backend.
  The Windows backend needs a real ISR → DPC → nvkm handler path, and a
  worker thread that calls `lkpi_work_run_pending()`.

## Running nvkm on the real card from Linux (`nvkm_vfio`)

This is the fastest way to debug chip bring-up: the same nvkm objects and
shim as the Windows driver, in a process, with full logs. It is untested;
treat the first run as a test of the runner itself.

Prerequisites: IOMMU enabled; the GTX 1070 **and its HDMI audio function**
bound to `vfio-pci`; the card not driving any console; root (or enough
`RLIMIT_MEMLOCK` and access to `/dev/vfio`).

```sh
make -C src_driver nvkm-vfio
```

```sh
sudo build/host/nvkm_vfio --probe-only 0000:01:00.0
```

```sh
sudo build/host/nvkm_vfio --debug debug 0000:01:00.0
```

The first command only constructs the device: it reads the VBIOS and builds
nouveau's subdevices without initialising them. The second runs nouveau's full
device init, prints VBIOS version, VRAM size, the output table and the
temperature, then shuts the device down. Firmware is looked up under
`/lib/firmware/nvidia/gp104/`; distributions that ship it as `.zst` need it
decompressed into a directory passed with `--firmware`.

What a good run establishes: VBIOS shadowing works through PROM/PRAMIN,
devinit and FB init succeed (VRAM reported as 8192 MiB), the DCB lists the
card's DP/HDMI/DVI outputs, and — with firmware present — ACR/SEC2 load and
GR comes up. Each of those is a prerequisite for a later Windows stage.

If the host was already using the card or a guest exited uncleanly, reset
the GPU first (`test/reset_test_gpu.sh`): the secure-boot region stays
locked until reset and ACR load will fail.

## Next: stage 3

Size of the job, measured by linking `build/w64/libnvkm.a` and
`build/w32/libnvkm.a` whole with nothing else: the only unresolved symbols
are the shim's backend functions (about 25: locks, events, allocation, time,
log, `ioremap`, DMA pages, `request_irq`, PCI config), `memcpy`/`memset`/
`memcmp`, libgcc's 64-bit division and bit-count helpers (x86 mostly), and
the three `nvif_driver_*` tables the port must supply for its nvif client.
The x64 archive is about 4 MiB before dead-code elimination.

1. `lkpi/nt/lkpi_nt.c`: spinlocks on `KSPIN_LOCK`, mutexes on `KMUTEX`,
   events on `KEVENT`, pool allocation, `MmMapIoSpace`,
   `MmAllocateContiguousMemory` for DMA, a system worker thread, and
   `KeQueryPerformanceCounter` time. Extend the miniport's import allowlist
   accordingly.
2. Fill a `struct pci_dev` from VideoPort (IDs, BARs, interrupt) and call
   `nvkm_device_pci_new()` from `HwVidInitialize`, after the chip gate and
   with BIOS scanout left running.
3. Enable the interrupt in `HwVidFindAdapter` and route
   `HwVidInterrupt` → DPC → nouveau's handler.
4. An nvif client (the ~100 lines nouveau's DRM side has in
   `nouveau_nvif.c`) so device init is triggered the supported way and the
   later stages have object handles to work with.
5. Exit test: log shows VRAM size, EDID per connector and temperature while
   the VBE desktop keeps working.

Open question for stage 3: where nvkm's sleeping paths run. `HwVidInitialize`
is at PASSIVE_LEVEL, but `HwVidStartIO` is serialised by VideoPort and should
not block for long; mode sets that train DisplayPort links may need to be
handed to the worker thread.
