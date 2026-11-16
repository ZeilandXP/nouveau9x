/* XP nonpaged-memory/debug adapter for unmodified upstream OpenRM containers.
 * The allocator and red-black-tree algorithms are compiled from the submodule. */
#include "miniport.h"
#include "nvx_openrm_shim.h"
#include <nvport/nvport.h>
#include <containers/eheap_old.h>

#if defined(NVX_OPENRM_HOSTED)
#include <stdlib.h>
#include <stdio.h>
#else
extern PVOID NTAPI ExAllocatePoolWithTag(ULONG poolType,ULONG_PTR bytes,ULONG tag);
extern VOID NTAPI ExFreePool(PVOID pointer);
#endif
void *nvx_rm_alloc(NvLength bytes)
{
#if defined(NVX_OPENRM_HOSTED)
    return bytes?malloc((size_t)bytes):NULL;
#else
    return bytes?ExAllocatePoolWithTag(0,(ULONG_PTR)bytes,0x4852584eU):NULL;
#endif
}
void nvx_rm_free(void *pointer)
{
#if defined(NVX_OPENRM_HOSTED)
    free(pointer);
#else
    if(pointer) ExFreePool(pointer);
#endif
}
void *nvx_rm_set(void *pointer,NvU8 value,NvLength bytes)
{
    NvU8 *p=pointer;
    for(NvLength i=0;i<bytes;i++) p[i]=value;
    return pointer;
}
void nvx_rm_assert(const char *file,unsigned line)
{
#if defined(NVX_OPENRM_HOSTED)
    fprintf(stderr,"NVX: upstream container assertion %s:%u\n",file,line);
    abort();
#else
    VideoPortDebugPrint((VIDEO_DEBUG_LEVEL)0,"NVX: upstream container assertion %s:%u\n",file,line);
#endif
}

/* The GSP client namespace is shared by XP apps. Use their session token as
 * the OS isolation identity, so independently backed queues cannot share a
 * USERD page even when they belong to one RM client handle. */
static NvBool nvx_rm_same_owner(void *requester,void *owner)
{
    return !owner||(requester&&*(const NvU64 *)requester==*(const NvU64 *)owner);
}

typedef struct NVX_OPENRM_CHID_HEAPS {
    OBJEHEAP channelIds;
    OBJEHEAP fifoData;
} NVX_OPENRM_CHID_HEAPS;

NV_STATUS nvx_openrm_chid_heap_create(void **result)
{
    static NvU64 deviceOwner=0;
    PEMEMBLOCK reserved=NULL;
    NVX_OPENRM_CHID_HEAPS *heaps;
    OBJEHEAP *heap;
    NvU32 flags=NVOS32_ALLOC_FLAGS_FIXED_ADDRESS_ALLOCATE;
    NvU64 start=0,size=8;
    NV_STATUS status;
    if(!result) return NV_ERR_INVALID_ARGUMENT;
    *result=NULL;
    heaps=nvx_rm_alloc(sizeof(*heaps));
    if(!heaps) return NV_ERR_NO_MEMORY;
    nvx_rm_set(heaps,0,sizeof(*heaps));
    heap=&heaps->channelIds;
    constructObjEHeap(heap,0,2048,sizeof(NvU64),0);
    /* The upstream constructor is void; detect failure before using its lists. */
    if(!heap->pBlockList) {nvx_rm_free(heaps);return NV_ERR_NO_MEMORY;}
    /* Isolation must be enabled before allocating. Even the fixed device
     * reservation must provide the ownership comparator under isolation. */
    status=heap->eheapSetOwnerIsolation(heap,NV_TRUE,8);
    if(status==NV_OK)
        status=heap->eheapAlloc(heap,0x4e5658U,&flags,&start,&size,1,1,
                              &reserved,&deviceOwner,nvx_rm_same_owner);
    if(status!=NV_OK) {heap->eheapDestruct(heap);nvx_rm_free(heaps);return status;}
    reserved->pData=&deviceOwner;
    /* kfifoChidMgrAllocChid_IMPL mirrors each ID into pFifoDataHeap at the
     * same numeric offset. Keep that upstream association table in lockstep. */
    heap=&heaps->fifoData;
    constructObjEHeap(heap,0,2048,sizeof(NvU64),0);
    if(!heap->pBlockList) {
        heaps->channelIds.eheapDestruct(&heaps->channelIds);
        nvx_rm_free(heaps);
        return NV_ERR_NO_MEMORY;
    }
    *result=heaps;
    return NV_OK;
}

NV_STATUS nvx_openrm_chid_alloc(void *state,NvU64 *owner,NvU32 userdOffset,NvU32 *channelId)
{
    NVX_OPENRM_CHID_HEAPS *heaps=state;
    OBJEHEAP *heap;
    PEMEMBLOCK block=NULL,fifoBlock=NULL;
    NvU32 flags=NVOS32_ALLOC_FLAGS_FORCE_MEM_GROWS_UP|NVOS32_ALLOC_FLAGS_FORCE_INTERNAL_INDEX;
    NvU64 start=0,size=1;
    NV_STATUS status;
    if(!heaps||!owner||!*owner||!channelId||(userdOffset&0x1ffU)||userdOffset>0xfe00U)
        return NV_ERR_INVALID_ARGUMENT;
    heap=&heaps->channelIds;
    /* Same allocator mode used by kfifoChidMgrAllocChid_IMPL. Ownership and
     * whole-page exclusion are performed by upstream eheapAlloc itself. */
    status=heap->eheapAlloc(heap,0x4e5658U,&flags,&start,&size,
        (userdOffset&0xfffU)/0x200U,1,&block,owner,nvx_rm_same_owner);
    if(status!=NV_OK) return status;
    if(!block||start<8U||start>=2048U||
       (start&7U)!=((userdOffset&0xfffU)/0x200U)) {
        (void)heap->eheapFree(heap,start);
        return NV_ERR_INVALID_STATE;
    }
    {
        NvU32 fifoFlags=NVOS32_ALLOC_FLAGS_FIXED_ADDRESS_ALLOCATE;
        NvU64 fifoStart=start,fifoSize=1;
        OBJEHEAP *fifoHeap=&heaps->fifoData;
        status=fifoHeap->eheapAlloc(fifoHeap,0x4e5658U,&fifoFlags,
            &fifoStart,&fifoSize,1,1,&fifoBlock,NULL,NULL);
        if(status!=NV_OK||!fifoBlock||fifoStart!=start) {
            (void)heap->eheapFree(heap,start);
            return status==NV_OK?NV_ERR_INVALID_STATE:status;
        }
        /* Upstream stores KernelChannel here; the stable client-session
         * identity serves as the XP adapter's association and owner check. */
        fifoBlock->pData=owner;
    }
    block->pData=owner;
    *channelId=(NvU32)start;
    return NV_OK;
}

NV_STATUS nvx_openrm_chid_free(void *state,NvU64 *owner,NvU32 channelId)
{
    NVX_OPENRM_CHID_HEAPS *heaps=state;
    OBJEHEAP *heap,*fifoHeap;
    PEMEMBLOCK block;
    PEMEMBLOCK fifoBlock;
    NV_STATUS status;
    if(!heaps||!owner||channelId<8U) return NV_ERR_INVALID_ARGUMENT;
    heap=&heaps->channelIds;fifoHeap=&heaps->fifoData;
    block=heap->eheapGetBlock(heap,channelId,NV_FALSE);
    fifoBlock=fifoHeap->eheapGetBlock(fifoHeap,channelId,NV_FALSE);
    if(!block||block->pData!=owner||!fifoBlock||fifoBlock->pData!=owner)
        return NV_ERR_INVALID_ARGUMENT;
    /* kfifoChidMgrFreeChid_IMPL drops both mirrored allocations. */
    fifoBlock->pData=NULL;
    status=fifoHeap->eheapFree(fifoHeap,channelId);
    if(status!=NV_OK) return status;
    block->pData=NULL;
    return heap->eheapFree(heap,channelId);
}

/* Call only after confirmed RM-client teardown, when no hardware channel can
 * refer to an ID and no further client operations can enter this device. */
void nvx_openrm_chid_heap_destroy(void **state)
{
    NVX_OPENRM_CHID_HEAPS *heaps;
    if(!state||!*state) return;
    heaps=*state;
    heaps->fifoData.eheapDestruct(&heaps->fifoData);
    heaps->channelIds.eheapDestruct(&heaps->channelIds);
    nvx_rm_free(heaps);
    *state=NULL;
}
