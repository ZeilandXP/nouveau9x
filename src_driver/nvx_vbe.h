#ifndef NVX_VBE_H
#define NVX_VBE_H
#include "nvx_device.h"

/* video.h INT10 service ABI, including real-mode segment registers. */
typedef struct {
    ULONG Eax, Ebx, Ecx, Edx, Esi, Edi, Ebp;
    USHORT SegDs, SegEs;
} NVX_INT10_ARGS;
typedef struct {
    USHORT Size, Version;
    PVOID Context;
    VOID (VPAPI *Reference)(PVOID);
    VOID (VPAPI *Dereference)(PVOID);
    VP_STATUS (VPAPI *Allocate)(PVOID, PUSHORT, PUSHORT, PULONG);
    VP_STATUS (VPAPI *Free)(PVOID, USHORT, USHORT);
    VP_STATUS (VPAPI *Read)(PVOID, USHORT, USHORT, PVOID, ULONG);
    VP_STATUS (VPAPI *Write)(PVOID, USHORT, USHORT, PVOID, ULONG);
    VP_STATUS (VPAPI *Call)(PVOID, NVX_INT10_ARGS *);
} NVX_INT10_INTERFACE;

VP_STATUS VPAPI VideoPortQueryServices(PVOID extension, ULONG service, PVOID interface);

/* Build the mode table from the video BIOS and read the monitor EDID.
 * Returns FALSE when no verified linear-framebuffer mode exists. */
BOOLEAN NvxVbeInitialize(PVOID extension);
/* Program a mode from the table and confirm the BIOS reports it active. */
BOOLEAN NvxVbeSetMode(PVOID extension, ULONG index);
BOOLEAN NvxVbeSuccess(VP_STATUS status, ULONG eax);
/* 128-byte base EDID block: fixed header and zero checksum. */
BOOLEAN NvxEdidBlockValid(const UCHAR *edid);
#endif
