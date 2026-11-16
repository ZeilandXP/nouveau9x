/* XP resource views passed to unchanged upstream channel/context policy.
 * Stack views describe already allocated resources; upstream chooses buffer
 * initialization policy and token construction, not XP memory allocation. */
#include "nvx_openrm_policy_view.h"
#include "nvx_openrm_channel_flags_generated.h"

NV_STATUS nvx_openrm_work_submit_token(NvU32 chid,NvU32 runlist,NvU32 *token)
{
    KernelChannelGroup group={0};
    KernelChannelGroupApi api={&group};
    KernelChannel channel={chid,runlist,NV_TRUE,&api};
    OBJGPU gpu={0};
    KernelFifo fifo={0};
    if(!token||chid>=4096U||runlist>=128U) return NV_ERR_INVALID_ARGUMENT;
    return kfifoGenerateWorkSubmitToken_GA100(&gpu,&fifo,&channel,token,NV_TRUE);
}

NV_STATUS nvx_openrm_context_init_entry(NvU32 id,NvU64 physical,NvU64 size,
    NvBool initialized,NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY *entry,NvBool *add)
{
    MEMORY_DESCRIPTOR memory={physical,size,ADDR_SYSMEM,NV_MEMORY_UNCACHED};
    ENGINE_CTX_DESCRIPTOR engine={&memory};
    KernelChannelGroup group={{&engine}};
    KernelChannelGroupApi api={&group};
    KernelChannel channel={0,0,NV_FALSE,&api};
    KernelGraphicsContext context={0};
    KernelGraphics globals={0};
    GR_GLOBALCTX_BUFFER internal;
    OBJGPU gpu={0};
    if(!entry||!add||!size||(physical&4095U)||physical>~(NvU64)0-size)
        return NV_ERR_INVALID_ARGUMENT;
    nvx_mem_zero(entry,sizeof(*entry));
    *add=NV_FALSE;
    context.bKGrMainCtxBufferInitialized=initialized;
    context.bKGrPmCtxBufferInitialized=initialized;
    context.bKGrPatchCtxBufferInitialized=initialized;
    context.pmCtxswBuffer.pMemDesc=&memory;
    context.ctxPatchBuffer.pMemDesc=&memory;
    if(kgrctxGetGlobalContextBufferInternalId_IMPL(id,&internal)==NV_OK) {
        globals.memDesc[internal]=&memory;
        globals.bInitialized[internal]=initialized;
    } else if(id!=NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_MAIN&&
              id!=NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_PM&&
              id!=NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID_PATCH)
        return NV_ERR_INVALID_ARGUMENT;
    return kgrctxPrepareInitializeCtxBuffer_IMPL(&gpu,&context,&globals,
                                                 &channel,id,entry,add);
}

NvBool nvx_openrm_context_is_global(NvU32 id)
{
    GR_GLOBALCTX_BUFFER internal;
    return kgrctxGetGlobalContextBufferInternalId_IMPL(id,&internal)==NV_OK;
}

NV_STATUS nvx_openrm_kick(void *bar0,NvU32 barBytes,NvU32 runlistBase,
                           NvU32 channelId,NvU32 token)
{
    OBJGPU gpu={bar0};
    KernelFifo fifo={0};
    if(!bar0||barBytes<NV_VIRTUAL_FUNCTION_DOORBELL+sizeof(NvU32)||
       runlistBase>barBytes-sizeof(NvU32)-0x90U||channelId>=4096U)
        return NV_ERR_INVALID_ARGUMENT;
    /* Preserve the proven XP/GSP sequence: publish queue memory, ring the
     * host-RM doorbell, then notify the active runlist channel slot. */
    __sync_synchronize();
    if(kfifoUpdateUsermodeDoorbell_TU102(&gpu,&fifo,token,0)!=NV_OK)
        return NV_ERR_INVALID_STATE;
    nvx_reg_write32(bar0,runlistBase+0x90U,channelId);
    return NV_OK;
}

NvU32 nvx_openrm_channel_internal_flags(void)
{
    return DRF_DEF(_KERNELCHANNEL_ALLOC,_INTERNALFLAGS,_PRIVILEGE,_ADMIN)|
           DRF_DEF(_KERNELCHANNEL_ALLOC,_INTERNALFLAGS,_ERROR_NOTIFIER_TYPE,_CTXDMA)|
           DRF_DEF(_KERNELCHANNEL_ALLOC,_INTERNALFLAGS,_ECC_ERROR_NOTIFIER_TYPE,_NONE);
}
