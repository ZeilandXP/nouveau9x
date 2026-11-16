/*
 * nvx_openrm_shim.c - Hardware Abstraction Layer shim mapping VideoPort / VMM
 * primitives to NVIDIA OpenRM conventions.
 *
 * Implements unified register access, timing, memory manipulation, and
 * OpenRM MsgQ remote notification hooks for Windows XP/2000 and Win98.
 */

#include "miniport.h"
#include "nvx_openrm_shim.h"
#include "nvx_gsp.h"

NvU32 nvx_reg_read32(const void *base, NvU32 offset)
{
    if (!base) return 0xFFFFFFFFU;
    return VideoPortReadRegisterUlong((PULONG)((const UCHAR *)base + offset));
}

void nvx_reg_write32(void *base, NvU32 offset, NvU32 val)
{
    if (!base) return;
    VideoPortWriteRegisterUlong((PULONG)((UCHAR *)base + offset), val);
}

NvU8 nvx_reg_read08(const void *base, NvU32 offset)
{
    if (!base) return 0xFFU;
    return VideoPortReadRegisterUchar((PUCHAR)((const UCHAR *)base + offset));
}

void nvx_reg_write08(void *base, NvU32 offset, NvU8 val)
{
    if (!base) return;
    VideoPortWriteRegisterUchar((PUCHAR)((UCHAR *)base + offset), val);
}

void nvx_stall_us(NvU32 microseconds)
{
    VideoPortStallExecution(microseconds);
}

void nvx_delay_ms(NvU32 milliseconds)
{
    NvU32 i;
    for (i = 0; i < milliseconds; i++) {
        VideoPortStallExecution(1000);
    }
}

void nvx_mem_copy(void *dst, const void *src, NvLength bytes)
{
    if (!dst || !src || !bytes) return;
    VideoPortMoveMemory(dst, (PVOID)src, (ULONG)bytes);
}

void nvx_mem_zero(void *dst, NvLength bytes)
{
    if (!dst || !bytes) return;
    VideoPortZeroMemory(dst, (ULONG)bytes);
}

int nvx_msgq_notify_remote(int isRead, void *pArg)
{
    if (isRead == FCN_FLAG_NOTIFY_MSG_WRITE && pArg != NULL) {
        PNVX_DEVICE_EXTENSION dev = (PNVX_DEVICE_EXTENSION)pArg;
        if (dev->Bar0MmioVirtual != NULL) {
            VideoPortWriteRegisterUlong((PULONG)((PUCHAR)dev->Bar0MmioVirtual + 0x110c00), 0);
        }
    }
    return 0;
}

NvBool nvx_falcon_wait_idle(const void *bar0, NvU32 falconBase, NvU32 timeoutUs)
{
    NvU32 elapsed = 0;
    if (!bar0) return NV_FALSE;

    while (elapsed < timeoutUs) {
        /* NV_PFALCON_FALCON_DMACTL (0x10c) bit 1 (DMEM scrubbing) and bit 2 (IMEM scrubbing) */
        NvU32 dmactl = nvx_reg_read32(bar0, falconBase + 0x10c);
        if ((dmactl & 0x6) == 0) {
            return NV_TRUE;
        }
        nvx_stall_us(10);
        elapsed += 10;
    }
    return NV_FALSE;
}

/* kernel_channel.c::_kchannelSendChannelAllocRpc fixes USERD page/index to
 * the host-reserved ChID. kernel_fifo_ga100.c::kfifoGenerateWorkSubmitToken
 * encodes runlist and ChID with these published hardware fields. */
#include <alloc/alloc_channel.h>
#include <swref/published/ampere/ga100/dev_ctrl.h>
NvBool nvx_openrm_channel_identity(NvU32 channelId,NvU32 runlistId,
                                  NvU32 *allocationFlags,NvU32 *submitToken)
{
    if(!allocationFlags||!submitToken||channelId>=4096U||runlistId>=128U)
        return NV_FALSE;
    *allocationFlags=0x20U|
        DRF_DEF(OS04,_FLAGS,_CHANNEL_USERD_INDEX_PAGE_FIXED,_TRUE)|
        DRF_NUM(OS04,_FLAGS,_CHANNEL_USERD_INDEX_PAGE_VALUE,channelId/8U)|
        DRF_NUM(OS04,_FLAGS,_CHANNEL_USERD_INDEX_VALUE,channelId%8U);
    return nvx_openrm_work_submit_token(channelId,runlistId,submitToken)==NV_OK;
}

/* Descriptor construction follows kchannelAllocMem_GM107 and
 * _kchannelSendChannelAllocRpc. The backing allocation stays XP-owned.
 * Crucially RAMFC is a 512-byte subdescriptor at offset ZERO of RAMIN. */
#include <swref/published/maxwell/gm107/dev_ram.h>
#include <kernel/os/nv_memory_type.h>
NvBool nvx_openrm_channel_memory(NV_CHANNEL_ALLOC_PARAMS *p,NvU64 instance,
    NvU64 userd,NvU64 methods,NvU64 notifier)
{
    NV_MEMORY_DESC_PARAMS *d[5];
    NvU64 base[5]={instance,instance,userd,methods,notifier};
    NvU64 size[5]={NV_RAMIN_ALLOC_SIZE,DRF_SIZE(NV_RAMIN_RAMFC)/8,0x200,0x2000,0x1000};
    if(!p||(instance&(NV_RAMIN_ALLOC_SIZE-1U))||(userd&0x1ffU)||
       (methods&0xfffU)||(notifier&0xfffU)) return NV_FALSE;
    for(unsigned i=0;i<5;i++) if(!base[i]||base[i]>~(NvU64)0-size[i]) return NV_FALSE;
    if(userd<instance+NV_RAMIN_ALLOC_SIZE&&instance<userd+0x200U) return NV_FALSE;
    if(methods<instance+NV_RAMIN_ALLOC_SIZE&&instance<methods+0x2000U) return NV_FALSE;
    if(notifier<instance+NV_RAMIN_ALLOC_SIZE&&instance<notifier+0x1000U) return NV_FALSE;
    if(userd<methods+0x2000U&&methods<userd+0x200U) return NV_FALSE;
    if(userd<notifier+0x1000U&&notifier<userd+0x200U) return NV_FALSE;
    if(methods<notifier+0x1000U&&notifier<methods+0x2000U) return NV_FALSE;
    d[0]=&p->instanceMem;d[1]=&p->ramfcMem;d[2]=&p->userdMem;
    d[3]=&p->mthdbufMem;d[4]=&p->errorNotifierMem;
    for(unsigned i=0;i<5;i++) {
        d[i]->base=base[i];d[i]->size=size[i];
        d[i]->addressSpace=1; /* ADDR_SYSMEM */
        d[i]->cacheAttrib=NV_MEMORY_CACHED; /* VideoPort common buffer is coherent. */
    }
    return NV_TRUE;
}
