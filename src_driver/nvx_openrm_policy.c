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
    OBJGPU gpu={bar0,0};
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

/* Serialize at the XP caller. Complete the invalidate before backing reuse. */
static NV_STATUS nvx_openrm_invalidate_external_locked(void *bar0,NvU32 barBytes,NvU64 pdb)
{
    OBJGPU gpu={bar0,DRF_BASE(NV_VIRTUAL_FUNCTION_FULL_PHYS_OFFSET)};
    KernelGmmu mmu={0};
    TLB_INVALIDATE_PARAMS params={0};
    NV_STATUS status;
    NvU32 reg;
    if(!bar0||barBytes<gpu.vregOffset+NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE+4U||
       (pdb&4095U)||!pdb||pdb>=0x100000000ULL) return NV_ERR_INVALID_ARGUMENT;
    reg=GPU_VREG_RD32(&gpu,NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE);
    if(reg==0xffffffffU||(reg&0xffff0000U)==0xbadf0000U)
        return NV_ERR_NOT_SUPPORTED;
    params.timeout.remaining=200000U;
    status=kgmmuCheckPendingInvalidates_TU102(&gpu,&mmu,&params.timeout,0);
    if(status!=NV_OK) return status;
    params.pdbAddress=pdb;
    params.pdbAperture=NV_VIRTUAL_FUNCTION_PRIV_MMU_INVALIDATE_PDB_APERTURE_SYS_MEM;
    params.regVal=DRF_NUM(_VIRTUAL_FUNCTION_PRIV,_MMU_INVALIDATE,_TRIGGER,1U)|
                  DRF_DEF(_VIRTUAL_FUNCTION_PRIV,_MMU_INVALIDATE,_ALL_VA,_TRUE);
    /* PTE_DOWNGRADE: drain prior translations and writes before recycling. */
    (void)kgmmuSetTlbInvalidateMembarWarParameters_TU102(&gpu,&mmu,&params);
    status=kgmmuSetTlbInvalidationScope_GA100(&gpu,&mmu,
        NV_GMMU_INVAL_SCOPE_NON_LINK_TLBS,&params);
    if(status!=NV_OK) return status;
    __sync_synchronize();
    return kgmmuCommitTlbInvalidate_TU102(&gpu,&mmu,&params);
}

NV_STATUS nvx_openrm_invalidate_external(void *bar0,NvU32 barBytes,NvU64 pdb)
{
    static volatile NvU32 busy;
    NV_STATUS status;
    NvU32 wait;
    for(wait=0;wait<200000U;wait++) {
        if(!__sync_lock_test_and_set(&busy,1U)) break;
        nvx_stall_us(1);
    }
    if(wait==200000U) return NV_ERR_TIMEOUT;
    status=nvx_openrm_invalidate_external_locked(bar0,barBytes,pdb);
    __sync_lock_release(&busy);
    return status;
}
