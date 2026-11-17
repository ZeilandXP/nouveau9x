/*
 * NVIDIA Pascal (NV130: GP100..GP108) XDDM video miniport.
 * Targets: Windows XP x64 / Server 2003 x64 (NT 5.2) and Windows XP x86 (NT 5.1).
 *
 * Bring-up stage 1: BIOS (VBE) scanout with CPU 2D.
 *   - The chipset is identified from NV_PMC_BOOT_0, as nouveau does.
 *   - Modes come from the card's own video BIOS; nothing is programmed
 *     through the display engine yet.
 *   - The display driver's fill/copy/upload ABI is served on the CPU against
 *     the mapped scanout, so nvxdisp.dll runs unmodified.
 * The nouveau (nvkm) core replaces these pieces stage by stage: native
 * display, copy-engine 2D, then secure boot + GR for 3D.
 */

#include "nvx_device.h"
#include "nvx_log.h"
#include "nvx_vbe.h"
#include "nvx_swblt.h"

#define NVX_DRIVER_VERSION "4.0.0.1"
#define NV_VENDOR_ID 0x10DE
#define VIDEO_MEMORY_SPACE_P6CACHE 0x08UL   /* write-combined mapping */

/* Monitor child id reported to XP for the BIOS primary display. */
#define NVX_MONITOR_UID 0x00000100UL

BOOLEAN NTAPI HwVidInterrupt(PVOID HwDeviceExtension) {
    (void)HwDeviceExtension;
    /* No interrupt is connected until nvkm owns the device. */
    return FALSE;
}

BOOLEAN NTAPI HwVidResetHw(PVOID HwDeviceExtension, ULONG Columns, ULONG Rows) {
    (void)HwDeviceExtension;
    (void)Columns;
    (void)Rows;
    /* Called at bugcheck/high IRQL: touch nothing. Returning FALSE asks the
       HAL to restore text mode through the video BIOS, which is exactly the
       owner of scanout at this stage. */
    return FALSE;
}

VP_STATUS NTAPI HwVidSetPowerState(
    PVOID HwDeviceExtension,
    ULONG HwId,
    PVIDEO_POWER_MANAGEMENT VideoPowerControl
) {
    (void)HwDeviceExtension;
    (void)HwId;
    (void)VideoPowerControl;
    return NO_ERROR;
}

VP_STATUS NTAPI HwVidGetPowerState(
    PVOID HwDeviceExtension,
    ULONG HwId,
    PVIDEO_POWER_MANAGEMENT VideoPowerControl
) {
    (void)HwDeviceExtension;
    (void)HwId;
    if (VideoPowerControl) {
        VideoPowerControl->PowerState = VideoPowerOn;
    }
    return NO_ERROR;
}

VP_STATUS NTAPI HwVidGetVideoChildDescriptor(
    PVOID HwDeviceExtension,
    PVIDEO_CHILD_ENUM_INFO ChildEnumInfo,
    PVIDEO_CHILD_TYPE VideoChildType,
    PUCHAR pChildDescriptor,
    PULONG UId,
    PULONG pUnused
) {
    PNVX_DEVICE_EXTENSION dev=(PNVX_DEVICE_EXTENSION)HwDeviceExtension;
    if(pUnused) *pUnused=0;
    if(!ChildEnumInfo || !VideoChildType || !UId)
        return VIDEO_ENUM_INVALID_DEVICE;
    /* Index 0 is reserved for ACPI-described children; this driver has none. */
    if(ChildEnumInfo->ChildIndex==0)
        return VIDEO_ENUM_INVALID_DEVICE;
    /* One monitor: the BIOS primary display, when its EDID could be read.
       Without an EDID XP keeps its default monitor. */
    if(ChildEnumInfo->ChildIndex!=1 || !dev->EdidValid)
        return VIDEO_ENUM_NO_MORE_DEVICES;
    if(!pChildDescriptor || ChildEnumInfo->ChildDescriptorSize<sizeof(dev->Edid))
        return VIDEO_ENUM_INVALID_DEVICE;
    VideoPortMoveMemory(pChildDescriptor,dev->Edid,sizeof(dev->Edid));
    *VideoChildType=Monitor;
    *UId=NVX_MONITOR_UID;
    LogStr("NVX [DISPLAY]: enumerated BIOS primary monitor from EDID\n");
    return VIDEO_ENUM_MORE_DEVICES;
}

VP_STATUS NTAPI HwVidFindAdapter(
    PVOID HwDeviceExtension,
    PVOID HwContext,
    PWSTR ArgumentString,
    PVIDEO_PORT_CONFIG_INFO ConfigInfo,
    PUCHAR Again
) {
    PNVX_DEVICE_EXTENSION devExt = (PNVX_DEVICE_EXTENSION)HwDeviceExtension;
    VIDEO_ACCESS_RANGE accessRanges[8];
    USHORT pciId[2] = {0, 0};
    USHORT pciCmd = 0;
    UCHAR pciRevision = 0;
    const char *name;
    VP_STATUS status;
    ULONG i;

    (void)HwContext;
    (void)ArgumentString;

    NvxLogFlush();
    if (Again) *Again = 0;
    LogStr("NVX: HwVidFindAdapter entered\n");

    VideoPortZeroMemory(accessRanges, sizeof(accessRanges));
    status = VideoPortGetAccessRanges(HwDeviceExtension, 0, NULL, 8,
                                      accessRanges, NULL, NULL, &devExt->PciSlot);
    devExt->PciBusNumber = ConfigInfo->SystemIoBusNumber;
    LogStr("NVX: VideoPortGetAccessRanges status: ");
    LogHex(status);
    LogStr("\n");
    if (status != NO_ERROR)
        return ERROR_DEV_NOT_EXIST;

    VideoPortGetBusData(HwDeviceExtension, PCIConfiguration, 0, pciId, 0x00, sizeof(pciId));
    VideoPortGetBusData(HwDeviceExtension, PCIConfiguration, 0, &pciRevision, 0x08, sizeof(pciRevision));
    devExt->DeviceId = pciId[1];
    devExt->RevisionId = pciRevision;
    LogStr("NVX: PCI Vendor: ");
    LogHex(pciId[0]);
    LogStr(" Device: ");
    LogHex(pciId[1]);
    name = NvxPascalPciName(pciId[1]);
    if (name) { LogStr(" ('"); LogStr(name); LogStr("')"); }
    LogStr("\n");
    if (pciId[0] != NV_VENDOR_ID) {
        LogStr("NVX: not an NVIDIA device; declining.\n");
        return ERROR_DEV_NOT_EXIST;
    }

    /* Memory decode is required for BAR0; bus mastering is needed later for
       DMA and is harmless now. */
    VideoPortGetBusData(HwDeviceExtension, PCIConfiguration, 0, &pciCmd, 0x04, sizeof(pciCmd));
    if ((pciCmd & 0x0006) != 0x0006) {
        pciCmd |= 0x0007; /* I/O + Memory + Bus Master */
        VideoPortSetBusData(HwDeviceExtension, PCIConfiguration, 0, &pciCmd, 0x04, sizeof(pciCmd));
        LogStr("NVX: enabled PCI memory decode and bus mastering: ");
        LogHex(pciCmd);
        LogStr("\n");
    }

    /* Pascal exposes BAR0 = 16 MiB registers, BAR1 = VRAM aperture (64 MiB or
       more), BAR3 = 32 MiB instance-memory window. Ranges arrive in BAR order,
       so the first small memory range is BAR0 and the first large one BAR1. */
    for (i = 0; i < 8; i++) {
        if (accessRanges[i].RangeLength == 0) continue;
        LogStr("  Range: Start=");
        LogHex(accessRanges[i].RangeStart.QuadPart);
        LogStr(" Len=");
        LogHex(accessRanges[i].RangeLength);
        LogStr(" Io=");
        LogHex(accessRanges[i].RangeInIoSpace);
        LogStr("\n");
        if (accessRanges[i].RangeInIoSpace) continue;
        if (devExt->PhysicalBar0.QuadPart == 0 && accessRanges[i].RangeLength <= 0x2000000) {
            devExt->PhysicalBar0 = accessRanges[i].RangeStart;
            devExt->Bar0Length = accessRanges[i].RangeLength;
        } else if (devExt->PhysicalBar1.QuadPart == 0 && accessRanges[i].RangeLength >= 0x4000000) {
            devExt->PhysicalBar1 = accessRanges[i].RangeStart;
            devExt->Bar1Length = accessRanges[i].RangeLength;
        }
    }
    if (devExt->PhysicalBar0.QuadPart == 0 || devExt->PhysicalBar1.QuadPart == 0) {
        /* Never guess a physical address: a wrong one maps someone else's
           device. */
        LogStr("NVX: required BAR0/BAR1 not assigned; declining.\n");
        NvxLogFlush();
        return ERROR_DEV_NOT_EXIST;
    }
    LogStr("NVX: Selected BAR0=");
    LogHex(devExt->PhysicalBar0.QuadPart);
    LogStr(" BAR1=");
    LogHex(devExt->PhysicalBar1.QuadPart);
    LogStr("\n");

    /* No interrupt until nvkm is present; zero keeps videoprt from calling
       IoConnectInterrupt. */
    ConfigInfo->BusInterruptLevel = 0;
    ConfigInfo->BusInterruptVector = 0;
    ConfigInfo->VdmPhysicalVideoMemoryAddress.QuadPart = 0;
    ConfigInfo->VdmPhysicalVideoMemoryLength = 0;

    devExt->NumModes = 0;
    devExt->CurrentModeIndex = 0;

    LogStr("NVX: HwVidFindAdapter returning NO_ERROR\n");
    NvxLogFlush();
    return NO_ERROR;
}

BOOLEAN NTAPI HwVidInitialize(PVOID HwDeviceExtension) {
    PNVX_DEVICE_EXTENSION devExt = (PNVX_DEVICE_EXTENSION)HwDeviceExtension;

    NvxLogFlush();
    LogStr("NVX: HwVidInitialize entered\n");

    devExt->Bar0MmioVirtual = VideoPortGetDeviceBase(HwDeviceExtension,
        devExt->PhysicalBar0, devExt->Bar0Length, FALSE);
    if (!devExt->Bar0MmioVirtual) {
        LogStr("NVX: BAR0 mapping failed.\n");
        NvxLogFlush();
        return FALSE;
    }
    if (!NvxChipDecode(VideoPortReadRegisterUlong((PULONG)devExt->Bar0MmioVirtual),
                       &devExt->Chip)) {
        LogStr("NVX: NV_PMC_BOOT_0=");
        LogHex(devExt->Chip.Boot0);
        LogStr(" is not a decodable chipset; device not responding.\n");
        NvxLogFlush();
        return FALSE;
    }
    LogStr("NVX: NV_PMC_BOOT_0=");
    LogHex(devExt->Chip.Boot0);
    LogStr(" chipset=");
    LogHex(devExt->Chip.Chipset);
    LogStr(" (");
    LogStr(devExt->Chip.Name);
    LogStr(") rev=");
    LogHex(devExt->Chip.Revision);
    LogStr("\n");
    if (devExt->Chip.Family != NVX_FAMILY_PASCAL)
        LogStr("NVX: WARNING: not an NV130 (Pascal) chipset; only BIOS scanout will ever be used.\n");

    if (!NvxVbeInitialize(HwDeviceExtension)) {
        LogStr("NVX: no usable BIOS modes; failing initialization so XP keeps VGA.\n");
        NvxLogFlush();
        return FALSE;
    }
    LogStr("NVX: BIOS mode table entries: ");
    LogHex(devExt->NumModes);
    LogStr("\n");
    LogStr("NVX: HwVidInitialize returning TRUE\n");
    NvxLogFlush();
    return TRUE;
}

/* The software 2D surface is the active mode's scanout, when one is mapped. */
static BOOLEAN NvxSoftSurface(PNVX_DEVICE_EXTENSION dev, NVX_SURFACE *surface)
{
    const VIDEO_MODE_INFORMATION *mode;
    if (!dev->ModeIsSet || !dev->ScanoutKernelVirtual ||
        dev->CurrentModeIndex >= dev->NumModes)
        return FALSE;
    mode = &dev->Modes[dev->CurrentModeIndex];
    surface->Base = (uint8_t *)dev->ScanoutKernelVirtual;
    surface->Bytes = (uint64_t)mode->ScreenStride * mode->VisScreenHeight;
    surface->GpuVa = NVX_SOFT_SCANOUT_VA;
    return surface->Bytes <= dev->ScanoutKernelLength;
}

static ULONG NvxBltStatus(NVX_BLT_STATUS s)
{
    return s == NVX_BLT_OK ? NO_ERROR :
           s == NVX_BLT_UNSUPPORTED ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

BOOLEAN NTAPI HwVidStartIO(
    PVOID HwDeviceExtension,
    PVIDEO_REQUEST_PACKET RequestPacket
) {
    PNVX_DEVICE_EXTENSION devExt = (PNVX_DEVICE_EXTENSION)HwDeviceExtension;
    PSTATUS_BLOCK statusBlock = RequestPacket->StatusBlock;
    NVX_SURFACE surface;
    ULONG status = NO_ERROR;

    statusBlock->Information = 0;

    switch (RequestPacket->IoControlCode) {
    case IOCTL_VIDEO_QUERY_NUM_AVAIL_MODES: {
        PVIDEO_NUM_MODES numModes = (PVIDEO_NUM_MODES)RequestPacket->OutputBuffer;
        if (RequestPacket->OutputBufferLength < sizeof(VIDEO_NUM_MODES)) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        numModes->NumModes = devExt->NumModes;
        numModes->ModeInformationLength = sizeof(VIDEO_MODE_INFORMATION);
        statusBlock->Information = sizeof(VIDEO_NUM_MODES);
        break;
    }

    case IOCTL_VIDEO_QUERY_AVAIL_MODES: {
        ULONG requiredSize = devExt->NumModes * sizeof(VIDEO_MODE_INFORMATION);
        if (RequestPacket->OutputBufferLength < requiredSize) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        VideoPortMoveMemory(RequestPacket->OutputBuffer, devExt->Modes, requiredSize);
        statusBlock->Information = requiredSize;
        break;
    }

    case IOCTL_VIDEO_QUERY_CURRENT_MODE: {
        if (RequestPacket->OutputBufferLength < sizeof(VIDEO_MODE_INFORMATION)) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        if (devExt->CurrentModeIndex >= devExt->NumModes) {
            status = ERROR_INVALID_PARAMETER;
            break;
        }
        VideoPortMoveMemory(RequestPacket->OutputBuffer,
                            &devExt->Modes[devExt->CurrentModeIndex],
                            sizeof(VIDEO_MODE_INFORMATION));
        statusBlock->Information = sizeof(VIDEO_MODE_INFORMATION);
        break;
    }

    case IOCTL_VIDEO_SET_CURRENT_MODE: {
        PVIDEO_MODE mode = (PVIDEO_MODE)RequestPacket->InputBuffer;
        ULONG requested;
        if (RequestPacket->InputBufferLength < sizeof(VIDEO_MODE)) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        requested = mode->RequestedMode & 0x3fffffffUL; /* strip NO_ZERO_MEMORY/MAP_MEM_LINEAR */
        if (requested >= devExt->NumModes) {
            status = ERROR_INVALID_PARAMETER;
            break;
        }
        LogStr("NVX: SET_CURRENT_MODE index ");
        LogHex(requested);
        LogStr("\n");
        if (!NvxVbeSetMode(HwDeviceExtension, requested)) {
            /* The BIOS may have left an undefined mode; scanout is no longer
               known, so stop serving 2D until a mode set succeeds. */
            devExt->ModeIsSet = FALSE;
            status = ERROR_INVALID_PARAMETER;
        }
        break;
    }

    case IOCTL_VIDEO_MAP_VIDEO_MEMORY: {
        PVIDEO_MEMORY videoMemory = (PVIDEO_MEMORY)RequestPacket->InputBuffer;
        PVIDEO_MEMORY_INFORMATION memInfo = (PVIDEO_MEMORY_INFORMATION)RequestPacket->OutputBuffer;
        ULONG inIoSpace = VIDEO_MEMORY_SPACE_P6CACHE;
        PVOID mappedVirtual;
        ULONG length;
        if (RequestPacket->OutputBufferLength < sizeof(VIDEO_MEMORY_INFORMATION) ||
            RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY)) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        if (!devExt->ModeIsSet || devExt->CurrentModeIndex >= devExt->NumModes) {
            status = ERROR_INVALID_PARAMETER;
            break;
        }
        length = devExt->Modes[devExt->CurrentModeIndex].ScreenStride *
                 devExt->Modes[devExt->CurrentModeIndex].VisScreenHeight;
        mappedVirtual = videoMemory->RequestedVirtualAddress;
        status = VideoPortMapMemory(HwDeviceExtension,
            devExt->ModeFramebuffer[devExt->CurrentModeIndex],
            &length, &inIoSpace, &mappedVirtual);
        if (status != NO_ERROR) {
            /* Retry without write combining rather than fail the desktop. */
            LogStr("NVX: write-combined scanout mapping refused; retrying uncached.\n");
            inIoSpace = 0;
            length = devExt->Modes[devExt->CurrentModeIndex].ScreenStride *
                     devExt->Modes[devExt->CurrentModeIndex].VisScreenHeight;
            mappedVirtual = videoMemory->RequestedVirtualAddress;
            status = VideoPortMapMemory(HwDeviceExtension,
                devExt->ModeFramebuffer[devExt->CurrentModeIndex],
                &length, &inIoSpace, &mappedVirtual);
        }
        LogStr("NVX: map scanout physical ");
        LogHex(devExt->ModeFramebuffer[devExt->CurrentModeIndex].QuadPart);
        LogStr(" length ");
        LogHex(length);
        LogStr(" status ");
        LogHex(status);
        LogStr(" virtual ");
        LogHex((unsigned long long)(ULONG_PTR)mappedVirtual);
        LogStr("\n");
        if (status == NO_ERROR) {
            memInfo->VideoRamBase = mappedVirtual;
            memInfo->VideoRamLength = length;
            memInfo->FrameBufferBase = mappedVirtual;
            memInfo->FrameBufferLength = length;
            /* System-space mapping: valid in every process context, so the
               CPU 2D path below can use it from any StartIO call. */
            devExt->ScanoutKernelVirtual = mappedVirtual;
            devExt->ScanoutKernelLength = length;
            statusBlock->Information = sizeof(VIDEO_MEMORY_INFORMATION);
        }
        break;
    }

    case IOCTL_VIDEO_UNMAP_VIDEO_MEMORY: {
        PVIDEO_MEMORY videoMemory = (PVIDEO_MEMORY)RequestPacket->InputBuffer;
        if (RequestPacket->InputBufferLength < sizeof(VIDEO_MEMORY)) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        if (videoMemory->RequestedVirtualAddress == devExt->ScanoutKernelVirtual) {
            devExt->ScanoutKernelVirtual = NULL;
            devExt->ScanoutKernelLength = 0;
        }
        status = VideoPortUnmapMemory(HwDeviceExtension,
                                      videoMemory->RequestedVirtualAddress, 0);
        break;
    }

    case IOCTL_VIDEO_RESET_DEVICE:
    case IOCTL_VIDEO_SET_COLOR_REGISTERS:
        status = NO_ERROR;
        break;

    case IOCTL_NVX_SCANOUT_GPU_VA: {
        if (!RequestPacket->OutputBuffer || RequestPacket->OutputBufferLength < sizeof(ULONG64)) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        if (!NvxSoftSurface(devExt, &surface)) {
            LogStr("NVX [SCANOUT-VA]: rejected; no mapped active scanout.\n");
            status = ERROR_DEVICE_NOT_READY;
            break;
        }
        *(PULONG64)RequestPacket->OutputBuffer = surface.GpuVa;
        statusBlock->Information = sizeof(ULONG64);
        break;
    }

    case IOCTL_NVX_ACCEL_FILL: {
        PNVX_ACCEL_FILL_PARAMS params = (PNVX_ACCEL_FILL_PARAMS)RequestPacket->InputBuffer;
        if (!params || RequestPacket->InputBufferLength != sizeof(NVX_ACCEL_FILL_PARAMS)) {
            status = ERROR_INVALID_PARAMETER;
            break;
        }
        if (!NvxSoftSurface(devExt, &surface)) {
            status = ERROR_NOT_SUPPORTED;
            break;
        }
        status = NvxBltStatus(NvxSwFill(&surface, params->DstGpuVa, params->Color, params->Bytes));
        if (status == NO_ERROR) {
            statusBlock->Information = sizeof(NVX_ACCEL_FILL_PARAMS);
            devExt->AccelFills++;
        }
        break;
    }

    case IOCTL_NVX_ACCEL_COPY: {
        PNVX_ACCEL_COPY_PARAMS params = (PNVX_ACCEL_COPY_PARAMS)RequestPacket->InputBuffer;
        if (!params || RequestPacket->InputBufferLength < sizeof(NVX_ACCEL_COPY_PARAMS)) {
            status = ERROR_INVALID_PARAMETER;
            break;
        }
        if (!NvxSoftSurface(devExt, &surface)) {
            status = ERROR_NOT_SUPPORTED;
            break;
        }
        status = NvxBltStatus(NvxSwCopy(&surface, params->DstGpuVa, params->SrcGpuVa,
                                        params->BytesPerLine, params->SrcPitch,
                                        params->DstPitch, params->LineCount));
        if (status == NO_ERROR) {
            statusBlock->Information = sizeof(NVX_ACCEL_COPY_PARAMS);
            devExt->AccelCopies++;
        }
        break;
    }

    case IOCTL_NVX_ACCEL_UPLOAD: {
        PNVX_ACCEL_UPLOAD_PARAMS params = (PNVX_ACCEL_UPLOAD_PARAMS)RequestPacket->InputBuffer;
        ULONG headerBytes = FIELD_OFFSET(NVX_ACCEL_UPLOAD_PARAMS, Data);
        if (!params || RequestPacket->InputBufferLength < headerBytes ||
            params->Reserved > NVX_UPLOAD_KIND_TEXTOUT || !params->Bytes ||
            params->Bytes > NVX_BLT_MAX_BYTES ||
            RequestPacket->InputBufferLength != headerBytes + params->Bytes) {
            status = ERROR_INVALID_PARAMETER;
            break;
        }
        if (!NvxSoftSurface(devExt, &surface)) {
            status = ERROR_NOT_SUPPORTED;
            break;
        }
        status = NvxBltStatus(NvxSwUpload(&surface, params->DstGpuVa, params->Data, params->Bytes));
        if (status == NO_ERROR) {
            statusBlock->Information = headerBytes + params->Bytes;
            devExt->AccelUploads++;
        }
        break;
    }

    case IOCTL_NVX_DIAGNOSTIC_TRACE: {
        const ULONG *trace = (const ULONG *)RequestPacket->InputBuffer;
        if (!trace || RequestPacket->InputBufferLength < 4 * sizeof(ULONG)) {
            status = ERROR_INVALID_PARAMETER;
            break;
        }
        LogStr("NVX [GDI-TRACE]: stage="); LogHex(trace[0]);
        LogStr(" v0="); LogHex(trace[1]);
        LogStr(" v1="); LogHex(trace[2]);
        LogStr(" v2="); LogHex(trace[3]); LogStr("\n");
        break;
    }

    case IOCTL_NVX_DIAGNOSTIC_STATE: {
        PNVX_DIAGNOSTIC_STATE state = (PNVX_DIAGNOSTIC_STATE)RequestPacket->OutputBuffer;
        if (!state || RequestPacket->OutputBufferLength < sizeof(*state)) {
            status = ERROR_INSUFFICIENT_BUFFER;
            break;
        }
        /* Flag bits keep the display driver's existing meaning: 0x2 engine
           present, 0x10 2D path usable, 0x100 scanout mapped. */
        VideoPortZeroMemory(state, sizeof(*state));
        state->Flags = 0x00000002UL;
        if (NvxSoftSurface(devExt, &surface)) {
            state->Flags |= 0x00000010UL | 0x00000100UL;
            state->GspVramGpuVa = surface.GpuVa;
        }
        state->MonitorCount = devExt->EdidValid ? 1 : 0;
        state->NumModes = devExt->NumModes;
        state->CurrentModeIndex = devExt->CurrentModeIndex;
        statusBlock->Information = sizeof(*state);
        break;
    }

    case IOCTL_NVX_GET_LOG: {
        if (!RequestPacket->OutputBuffer || !RequestPacket->OutputBufferLength) {
            status = ERROR_INVALID_PARAMETER;
            break;
        }
        statusBlock->Information = NvxLogRead((PUCHAR)RequestPacket->OutputBuffer,
                                              RequestPacket->OutputBufferLength);
        break;
    }

    /* 3D needs the GR engine, which arrives with the nvkm port. Reporting
       "not supported" makes the OpenGL ICD fall back to software. */
    case IOCTL_NVX_3D_QUERY_CAPS:
    case IOCTL_NVX_3D_ALLOC_BUFFER:
    case IOCTL_NVX_3D_SUBMIT:
    case IOCTL_NVX_3D_WAIT_FENCE:
    case IOCTL_NVX_3D_FREE_BUFFER:
    case IOCTL_NVX_3D_UPLOAD_BUFFER:
    case IOCTL_NVX_3D_READBACK_BUFFER:
    case IOCTL_NVX_3D_NVC0_CLEAR:
    case IOCTL_NVX_3D_DOWNLOAD_BUFFER:
    case IOCTL_NVX_3D_VALIDATE_BATCH:
    case IOCTL_NVX_3D_NVC0_NATIVE_CLEAR:
    case IOCTL_NVX_3D_CLIENT_OPEN:
    case IOCTL_NVX_3D_CLIENT_CLOSE:
    case IOCTL_NVX_3D_CHANNEL_LEASE:
    case IOCTL_NVX_3D_SUBMIT_BATCH:
        status = ERROR_NOT_SUPPORTED;
        break;

    default:
        status = ERROR_INVALID_FUNCTION;
        break;
    }

    statusBlock->Status = status;
    return TRUE;
}

typedef LONG NTSTATUS;
#ifdef _WIN64
typedef NTSTATUS (*PDRIVER_ADD_DEVICE)(PVOID DriverObject, PVOID PhysicalDeviceObject);
typedef NTSTATUS (*PDRIVER_DISPATCH)(PVOID DeviceObject, PVOID Irp);
#else
typedef NTSTATUS (__stdcall *PDRIVER_ADD_DEVICE)(PVOID DriverObject, PVOID PhysicalDeviceObject);
typedef NTSTATUS (__stdcall *PDRIVER_DISPATCH)(PVOID DeviceObject, PVOID Irp);
#endif

static PDRIVER_ADD_DEVICE RealAddDevice = NULL;
static PDRIVER_DISPATCH RealPnpDispatch = NULL;
static PDRIVER_DISPATCH RealPowerDispatch = NULL;

#ifdef _WIN64
static NTSTATUS HookedAddDevice(PVOID DriverObject, PVOID PhysicalDeviceObject) {
#else
static NTSTATUS __stdcall HookedAddDevice(PVOID DriverObject, PVOID PhysicalDeviceObject) {
#endif
    LogStr("NVX: HookedAddDevice called! DrvObj=");
    LogHex((unsigned long long)(ULONG_PTR)DriverObject);
    LogStr(" PDO=");
    LogHex((unsigned long long)(ULONG_PTR)PhysicalDeviceObject);
    LogStr("\n");

    NTSTATUS status = RealAddDevice(DriverObject, PhysicalDeviceObject);

    LogStr("NVX: RealAddDevice returned: ");
    LogHex((unsigned long long)status);
    LogStr("\n");
    return status;
}

#ifdef _WIN64
static NTSTATUS HookedPnpDispatch(PVOID DeviceObject, PVOID Irp) {
    unsigned char *irpBytes = (unsigned char *)Irp;
    unsigned char **stackLocationPtr = (unsigned char **)(irpBytes + 0xb8);
#else
static NTSTATUS __stdcall HookedPnpDispatch(PVOID DeviceObject, PVOID Irp) {
    unsigned char *irpBytes = (unsigned char *)Irp;
    unsigned char **stackLocationPtr = (unsigned char **)(irpBytes + 0x60);
#endif
    unsigned char *stack = *stackLocationPtr;
    unsigned char major = stack ? stack[0] : 0xFF;
    unsigned char minor = stack ? stack[1] : 0xFF;

    LogStr("NVX: HookedPnpDispatch! Major=");
    LogHex(major);
    LogStr(" Minor=");
    LogHex(minor);
    LogStr(" DevObj=");
    LogHex((unsigned long long)(ULONG_PTR)DeviceObject);
    LogStr("\n");

    NTSTATUS status = RealPnpDispatch(DeviceObject, Irp);

    LogStr("NVX: RealPnpDispatch Major=");
    LogHex(major);
    LogStr(" Minor=");
    LogHex(minor);
    LogStr(" returned: ");
    LogHex((unsigned long long)status);
    LogStr("\n");
    NvxLogFlush();
    return status;
}

#ifdef _WIN64
static NTSTATUS HookedPowerDispatch(PVOID DeviceObject, PVOID Irp) {
#else
static NTSTATUS __stdcall HookedPowerDispatch(PVOID DeviceObject, PVOID Irp) {
#endif
    LogStr("NVX: HookedPowerDispatch!\n");
    NTSTATUS status = RealPowerDispatch(DeviceObject, Irp);
    NvxLogFlush();
    return status;
}

#ifdef _WIN64
ULONG DriverEntry(PVOID Context1, PVOID Context2) {
#else
ULONG NTAPI DriverEntry(PVOID Context1, PVOID Context2) {
#endif
    VIDEO_HW_INITIALIZATION_DATA hwInitData;
    ULONG status;

    LogStr("\n========================================\n");
    LogStr(" NVX Pascal Video Miniport " NVX_DRIVER_VERSION " (BIOS scanout, CPU 2D)\n");
    LogStr("========================================\n");

    VideoPortZeroMemory(&hwInitData, sizeof(VIDEO_HW_INITIALIZATION_DATA));

    hwInitData.HwInitDataSize = sizeof(VIDEO_HW_INITIALIZATION_DATA);
    hwInitData.AdapterInterfaceType = PCIBus;
    hwInitData.HwFindAdapter = HwVidFindAdapter;
    hwInitData.HwInitialize = HwVidInitialize;
    hwInitData.HwInterrupt = HwVidInterrupt;
    hwInitData.HwStartIO = HwVidStartIO;
    hwInitData.HwResetHw = HwVidResetHw;
    hwInitData.HwDeviceExtensionSize = sizeof(NVX_DEVICE_EXTENSION);
    hwInitData.StartingDeviceNumber = 0;

    hwInitData.HwSetPowerState = HwVidSetPowerState;
    hwInitData.HwGetPowerState = HwVidGetPowerState;
    hwInitData.HwGetVideoChildDescriptor = HwVidGetVideoChildDescriptor;

    status = VideoPortInitialize(Context1, Context2, &hwInitData, NULL);
    LogStr("NVX: VideoPortInitialize returned: ");
    LogHex(status);
    LogStr("\n");

#ifdef __VXDKERNEL__
    NvxLogFlush();
    return status;
#else
    if (status == 0) {
        unsigned char *drvObj = (unsigned char *)Context1;
#ifdef _WIN64
        unsigned char *drvExt = *(unsigned char **)(drvObj + 0x30);
        ULONG addDeviceOffset = 0x08;
        ULONG pnpDispatchOffset = 0x148;
        ULONG powerDispatchOffset = 0x120;
#else
        unsigned char *drvExt = *(unsigned char **)(drvObj + 0x18);
        ULONG addDeviceOffset = 0x04;
        ULONG pnpDispatchOffset = 0xa4;
        ULONG powerDispatchOffset = 0x90;
#endif
        if (drvExt) {
            RealAddDevice = *(PDRIVER_ADD_DEVICE *)(drvExt + addDeviceOffset);
            LogStr("NVX: Original AddDevice: ");
            LogHex((unsigned long long)(ULONG_PTR)RealAddDevice);
            LogStr("\n");
            *(PDRIVER_ADD_DEVICE *)(drvExt + addDeviceOffset) = HookedAddDevice;

            /*
             * videoprt.sys VpIsMultiFunctionSupported (at offset 0x25ad0 on x64, 0xd368 on x86) checks
             * if the GPU is a multi-function PCI device (bit 7 of PCI HeaderType 0x0E).
             * For a display + HDMI-audio card such as the GTX 1070, it queries MultiFunctionSupported from the registry,
             * and if absent or 0, returns FALSE (0), causing IRP_MN_START_DEVICE to fail with
             * STATUS_UNSUCCESSFUL (0xC0000001 / Code 10) before calling HwVidFindAdapter!
             * Patch VpIsMultiFunctionSupported to unconditionally return TRUE (1).
             */
#ifdef _WIN64
            if (RealAddDevice) {
                unsigned char *vpBase = (unsigned char *)RealAddDevice - 0x280e0;
                unsigned char *multiFunc = vpBase + 0x25ad0;
                if (multiFunc[0] == 0x4c && multiFunc[1] == 0x8b && multiFunc[2] == 0xdc) {
                    unsigned long long cr0;
                    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
                    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0 & ~(1ULL << 16)));

                    /* mov $1, %eax; ret */
                    multiFunc[0] = 0xb8;
                    multiFunc[1] = 0x01;
                    multiFunc[2] = 0x00;
                    multiFunc[3] = 0x00;
                    multiFunc[4] = 0x00;
                    multiFunc[5] = 0xc3;

                    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
                    LogStr("NVX: Patched VpIsMultiFunctionSupported at ");
                    LogHex((unsigned long long)multiFunc);
                    LogStr("\n");
                }
            }
#else
            if (RealAddDevice) {
                /*
                 * On 32-bit Windows XP videoprt.sys:
                 * RealAddDevice is at offset 0xea26 from videoprt base.
                 * VpIsMultiFunctionSupported is at offset 0xed8c from videoprt base.
                 * Exactly RealAddDevice + 0x366 bytes!
                 * Prologue: 8B FF 55 8B EC 81 EC 0C 01 00 00
                 */
                unsigned char *multiFunc = (unsigned char *)RealAddDevice + 0x366;
                BOOLEAN found = FALSE;

                if (multiFunc[0] == 0x8b && multiFunc[1] == 0xff && multiFunc[2] == 0x55 &&
                    multiFunc[3] == 0x8b && multiFunc[4] == 0xec && multiFunc[5] == 0x81 &&
                    multiFunc[6] == 0xec && multiFunc[7] == 0x0c && multiFunc[8] == 0x01) {
                    found = TRUE;
                } else {
                    /* Scan range [-0x1000, +0x1000] around RealAddDevice if layout shifts */
                    for (int off = -0x1000; off < 0x2000; off++) {
                        unsigned char *p = (unsigned char *)RealAddDevice + off;
                        if (p[0] == 0x8b && p[1] == 0xff && p[2] == 0x55 &&
                            p[3] == 0x8b && p[4] == 0xec && p[5] == 0x81 &&
                            p[6] == 0xec && p[7] == 0x0c && p[8] == 0x01) {
                            multiFunc = p;
                            found = TRUE;
                            break;
                        }
                    }
                }

                if (found) {
                    unsigned long cr0;
                    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
                    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0 & ~(1UL << 16)));

                    /* mov $1, %eax; ret $4 -> b8 01 00 00 00 c2 04 00 */
                    multiFunc[0] = 0xb8;
                    multiFunc[1] = 0x01;
                    multiFunc[2] = 0x00;
                    multiFunc[3] = 0x00;
                    multiFunc[4] = 0x00;
                    multiFunc[5] = 0xc2;
                    multiFunc[6] = 0x04;
                    multiFunc[7] = 0x00;

                    __asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0));
                    LogStr("NVX: Successfully Patched 32-bit VpIsMultiFunctionSupported at ");
                    LogHex((unsigned long long)(ULONG_PTR)multiFunc);
                    LogStr("\n");
                } else {
                    LogStr("NVX: Warning: 32-bit VpIsMultiFunctionSupported signature not matched at ");
                    LogHex((unsigned long long)(ULONG_PTR)multiFunc);
                    LogStr("\n");
                }
            }
#endif
        }

        RealPnpDispatch = *(PDRIVER_DISPATCH *)(drvObj + pnpDispatchOffset);
        LogStr("NVX: Original PnpDispatch: ");
        LogHex((unsigned long long)(ULONG_PTR)RealPnpDispatch);
        LogStr("\n");
        *(PDRIVER_DISPATCH *)(drvObj + pnpDispatchOffset) = HookedPnpDispatch;

        RealPowerDispatch = *(PDRIVER_DISPATCH *)(drvObj + powerDispatchOffset);
        *(PDRIVER_DISPATCH *)(drvObj + powerDispatchOffset) = HookedPowerDispatch;
    }
#endif

    NvxLogFlush();
    return status;
}
