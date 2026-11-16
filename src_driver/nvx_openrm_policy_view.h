/* Private read-only object views for selected upstream policy functions.
 * These are NOT ABI-compatible NVOC objects and must never escape this TU pair.
 * Supported configuration: one physical GPU, no MIG/VF/UVM or protected context.
 * Allocation, locking, ownership and GSP transport remain in the XP adapter. */
#ifndef NVX_OPENRM_POLICY_VIEW_H
#define NVX_OPENRM_POLICY_VIEW_H
#include "nvx_openrm_shim.h"
#include <nvmisc.h>
#include <utils/nvassert.h>
#include <utils/nvprintf.h>
#include <kernel/gpu/gr/kernel_graphics_context_buffers.h>
#include <kernel/os/nv_memory_type.h>
#include <swref/published/ampere/ga100/dev_ctrl.h>
#include <swref/published/turing/tu102/dev_vm.h>

#define ADDR_SYSMEM 1
#define ADDR_FBMEM 2
#define AT_GPU 0
#define RMCFG_FEATURE_PLATFORM_GSP 0 /* XP is host CPU RM, not GSP firmware. */
#define IS_VGPU_GSP_PLUGIN_OFFLOAD_ENABLED(gpu) NV_FALSE
#define IS_GFID_VF(gfid) ((gfid)!=0)

typedef struct { NvU64 base,Size; NvU32 addressSpace,cacheAttrib; } MEMORY_DESCRIPTOR;
typedef struct {
    NvBool bAllocated;
    NvBool bInitialized[GR_GLOBALCTX_BUFFER_COUNT];
    MEMORY_DESCRIPTOR *memDesc[GR_GLOBALCTX_BUFFER_COUNT];
} GR_GLOBALCTX_BUFFERS;
typedef struct { MEMORY_DESCRIPTOR *pMemDesc; } ENGINE_CTX_DESCRIPTOR;
typedef struct { ENGINE_CTX_DESCRIPTOR *ppEngCtxDesc[1]; } KernelChannelGroup;
typedef struct { KernelChannelGroup *pKernelChannelGroup; } KernelChannelGroupApi;
typedef struct {
    NvU32 ChID,runlistId;
    NvBool runlistSet;
    KernelChannelGroupApi *pKernelChannelGroupApi;
} KernelChannel;
typedef struct {
    NvBool bKGrMainCtxBufferInitialized,bKGrPmCtxBufferInitialized,bKGrPatchCtxBufferInitialized;
    struct { MEMORY_DESCRIPTOR *pMemDesc; } pmCtxswBuffer,ctxPatchBuffer;
    GR_GLOBALCTX_BUFFERS localCtxBuffer;
} KernelGraphicsContextUnicast;
typedef KernelGraphicsContextUnicast KernelGraphicsContext;
typedef GR_GLOBALCTX_BUFFERS KernelGraphics;
typedef struct { void *bar0; } OBJGPU;
#define GPU_VREG_WR32(gpu,offset,value) nvx_reg_write32((gpu)->bar0,(offset),(value))
typedef struct { NvU32 unused; } KernelFifo;

/* Unsupported virtualization paths return an error rather than invent state. */
static inline NV_STATUS vgpuGetCallingContextGfid(OBJGPU *g,NvU32 *id)
{ (void)g;*id=0;return NV_OK; }
static inline NV_STATUS kfifoGetVChIdForSChId_HAL(OBJGPU *g,KernelFifo *f,NvU32 c,NvU32 id,NvU32 e,NvU32 *out)
{ (void)g;(void)f;(void)c;(void)id;(void)e;(void)out;return NV_ERR_NOT_SUPPORTED; }
static inline NvU32 kchannelGetEngineType(KernelChannel *c) { (void)c;return NV2080_ENGINE_TYPE_GRAPHICS; }
static inline NvBool kchannelIsRunlistSet(OBJGPU *g,KernelChannel *c) { (void)g;return c->runlistSet; }
static inline NvU32 kchannelGetRunlistId(KernelChannel *c) { return c->runlistId; }
static inline NV_STATUS kfifoGenerateInternalWorkSubmitToken_HAL(OBJGPU *g,KernelFifo *f,KernelChannel *c)
{ (void)g;(void)f;(void)c;return NV_ERR_NOT_SUPPORTED; }
static inline NV_STATUS kgrctxGetUnicast(OBJGPU *g,KernelGraphicsContext *c,KernelGraphicsContextUnicast **out)
{ (void)g;if(!c) return NV_ERR_INVALID_ARGUMENT;*out=c;return NV_OK; }
static inline NvBool kchannelIsCtxBufferAllocSkipped(KernelChannel *c) { (void)c;return NV_FALSE; }
static inline NvU32 gpumgrGetSubDeviceInstanceFromGpu(OBJGPU *g) { (void)g;return 0; }
static inline NvU32 kchannelGetGfid(KernelChannel *c) { (void)c;return 0; }
static inline GR_GLOBALCTX_BUFFERS *kgraphicsGetGlobalCtxBuffers(OBJGPU *g,KernelGraphics *k,NvU32 id)
{ (void)g;(void)id;return k; }
static inline NvU32 memdescGetAddressSpace(MEMORY_DESCRIPTOR *m) { return m->addressSpace; }
static inline NvU32 memdescGetCpuCacheAttrib(MEMORY_DESCRIPTOR *m) { return m->cacheAttrib; }
static inline NvU64 memdescGetPhysAddr(MEMORY_DESCRIPTOR *m,NvU32 at,NvU64 offset)
{ (void)at;return m->base+offset; }
NV_STATUS kgrctxGetGlobalContextBufferInternalId_IMPL(NvU32,GR_GLOBALCTX_BUFFER *);
#define kgrctxGetGlobalContextBufferInternalId kgrctxGetGlobalContextBufferInternalId_IMPL
NV_STATUS kgrctxPrepareInitializeCtxBuffer_IMPL(OBJGPU *,KernelGraphicsContext *,KernelGraphics *,KernelChannel *,NvU32,NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ENTRY *,NvBool *);
NV_STATUS kfifoUpdateUsermodeDoorbell_TU102(OBJGPU *,KernelFifo *,NvU32,NvU32);
NV_STATUS kfifoGenerateWorkSubmitToken_GA100(OBJGPU *,KernelFifo *,KernelChannel *,NvU32 *,NvBool);
#endif
