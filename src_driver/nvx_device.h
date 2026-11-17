/*
 * Shared miniport state for the Pascal (NV130) XDDM driver.
 */
#ifndef NVX_DEVICE_H
#define NVX_DEVICE_H

#ifdef NVX_HOST_TEST
#include "host_videoport.h"
#else
#include "miniport.h"
#endif
#include "nvx_chip.h"

#define MAX_NVX_MODES 32

/* The display driver addresses the scanout surface by "GPU virtual address".
 * While scanout is the BIOS (VBE) linear framebuffer and 2D runs on the CPU,
 * this fixed cookie stands in for the address; the copy-engine backend will
 * replace it with the surface's real channel VA. It only has to be non-zero
 * and 4-byte aligned. */
#define NVX_SOFT_SCANOUT_VA 0x0000000020000000ULL

#pragma pack(push, 1)
/* VBE 2.0/3.0 Controller Information Block */
typedef struct _VBE_INFO_BLOCK {
    CHAR   VbeSignature[4];       /* "VESA" */
    USHORT VbeVersion;            /* 0x0200, 0x0300 */
    ULONG  OemStringPtr;          /* Real mode far pointer */
    ULONG  Capabilities;
    ULONG  VideoModePtr;          /* Real mode far pointer to mode list */
    USHORT TotalMemory;           /* 64KB blocks */
    USHORT OemSoftwareRev;
    ULONG  OemVendorNamePtr;
    ULONG  OemProductNamePtr;
    ULONG  OemProductRevPtr;
    UCHAR  Reserved[222];
    UCHAR  OemData[256];
} VBE_INFO_BLOCK, *PVBE_INFO_BLOCK;

/* VBE 2.0/3.0 Mode Information Block */
typedef struct _VBE_MODE_INFO {
    USHORT ModeAttributes;        /* bit 0: supported, bit 7: linear buffer */
    UCHAR  WinAAttributes;
    UCHAR  WinBAttributes;
    USHORT WinGranularity;
    USHORT WinSize;
    USHORT WinASegment;
    USHORT WinBSegment;
    ULONG  WinFuncPtr;
    USHORT BytesPerScanLine;
    USHORT XResolution;
    USHORT YResolution;
    UCHAR  XCharSize;
    UCHAR  YCharSize;
    UCHAR  NumberOfPlanes;
    UCHAR  BitsPerPixel;
    UCHAR  NumberOfBanks;
    UCHAR  MemoryModel;
    UCHAR  BankSize;
    UCHAR  NumberOfImagePages;
    UCHAR  Reserved1;
    UCHAR  RedMaskSize;
    UCHAR  RedFieldPosition;
    UCHAR  GreenMaskSize;
    UCHAR  GreenFieldPosition;
    UCHAR  BlueMaskSize;
    UCHAR  BlueFieldPosition;
    UCHAR  RsvdMaskSize;
    UCHAR  RsvdFieldPosition;
    UCHAR  DirectColorModeInfo;
    ULONG  PhysBasePtr;           /* 32-bit physical linear framebuffer address */
    ULONG  OffScreenMemOffset;
    USHORT OffScreenMemSize;
    USHORT LinBytesPerScanLine;
    UCHAR  BnkNumberOfImagePages, LinNumberOfImagePages;
    UCHAR  LinRedMaskSize, LinRedFieldPosition;
    UCHAR  LinGreenMaskSize, LinGreenFieldPosition;
    UCHAR  LinBlueMaskSize, LinBlueFieldPosition;
    UCHAR  LinRsvdMaskSize, LinRsvdFieldPosition;
    ULONG  MaxPixelClock;
    UCHAR  Reserved2[190];
} VBE_MODE_INFO, *PVBE_MODE_INFO;
#pragma pack(pop)

typedef struct _NVX_DEVICE_EXTENSION {
    /* PCI identity and resources */
    ULONG DeviceId;
    ULONG RevisionId;
    ULONG PciBusNumber;
    ULONG PciSlot;
    PHYSICAL_ADDRESS PhysicalBar0;      /* MMIO registers */
    ULONG Bar0Length;
    PVOID Bar0MmioVirtual;
    PHYSICAL_ADDRESS PhysicalBar1;      /* VRAM aperture */
    ULONG Bar1Length;
    NVX_CHIP_INFO Chip;                 /* decoded from NV_PMC_BOOT_0 */

    /* Mode table. Entries come from the video BIOS until native modesetting
     * exists; ModeFramebuffer is the physical scanout base for that mode. */
    ULONG CurrentModeIndex;
    ULONG NumModes;
    VIDEO_MODE_INFORMATION Modes[MAX_NVX_MODES];
    ULONG ModeVbeIds[MAX_NVX_MODES];
    PHYSICAL_ADDRESS ModeFramebuffer[MAX_NVX_MODES];
    BOOLEAN ModeIsSet;
    ULONG ActiveWidth;
    ULONG ActiveHeight;
    ULONG ActiveBpp;
    ULONG ActiveFreq;

    /* Kernel mapping of the active scanout, owned by the software 2D path. */
    PVOID ScanoutKernelVirtual;
    ULONG ScanoutKernelLength;

    /* Primary monitor EDID as read through VBE/DDC, when the BIOS offers it. */
    BOOLEAN EdidValid;
    UCHAR Edid[128];

    /* 2D operation counters, reported through the diagnostic log. */
    ULONG AccelFills;
    ULONG AccelCopies;
    ULONG AccelUploads;
} NVX_DEVICE_EXTENSION, *PNVX_DEVICE_EXTENSION;

void LogStr(const char *s);
void LogHex(unsigned long long value);

#endif
