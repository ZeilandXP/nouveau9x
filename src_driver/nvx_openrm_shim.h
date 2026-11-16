/*
 * nvx_openrm_shim.h - Hardware Abstraction Layer shim mapping VideoPort / VMM
 * primitives to NVIDIA OpenRM conventions.
 *
 * Designed for portable compilation on Windows XP x64 (64-bit),
 * Windows 2000 / XP (32-bit XDDM), and Windows 98 SE (32-bit VxD).
 */

#ifndef NVX_OPENRM_SHIM_H
#define NVX_OPENRM_SHIM_H

#include "nvtypes.h"
#include "nvstatus.h"
#include "include/nvx_nvidia_open_shim.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register read/write abstraction */
NvU32 nvx_reg_read32(const void *base, NvU32 offset);
void  nvx_reg_write32(void *base, NvU32 offset, NvU32 val);
NvU8  nvx_reg_read08(const void *base, NvU32 offset);
void  nvx_reg_write08(void *base, NvU32 offset, NvU8 val);

/* Convenience macros matching OpenRM conventions */
#define GPU_REG_RD32(base, offset)        nvx_reg_read32((base), (offset))
#define GPU_REG_WR32(base, offset, val)   nvx_reg_write32((base), (offset), (val))
#define GPU_REG_RD08(base, offset)        nvx_reg_read08((base), (offset))
#define GPU_REG_WR08(base, offset, val)   nvx_reg_write08((base), (offset), (val))

/* Host-RM channel identity adapter. IDs are explicitly reserved in the
 * GSP allocation flags; no dependence on allocation order or a boot channel. */
NvBool nvx_openrm_channel_identity(NvU32 channelId, NvU32 runlistId,
                                  NvU32 *allocationFlags, NvU32 *submitToken);

/* Serialized by the caller's GR lifecycle lock; owner storage stays alive
 * until confirmed RM channel teardown and successful ID release. */
NV_STATUS nvx_openrm_chid_heap_create(void **result);
void nvx_openrm_chid_heap_destroy(void **state);
NV_STATUS nvx_openrm_chid_alloc(void *, NvU64 *, NvU32 userdOffset, NvU32 *channelId);
NV_STATUS nvx_openrm_chid_free(void *, NvU64 *, NvU32 channelId);

/* Delays and timing */
void  nvx_stall_us(NvU32 microseconds);
void  nvx_delay_ms(NvU32 milliseconds);

/* Memory operations */
void  nvx_mem_copy(void *dst, const void *src, NvLength bytes);
void  nvx_mem_zero(void *dst, NvLength bytes);

/* OpenRM MsgQ Doorbell Notification callback */
int   nvx_msgq_notify_remote(int isRead, void *pArg);

/* Falcon engine reset and polling helper */
NvBool nvx_falcon_wait_idle(const void *bar0, NvU32 falconBase, NvU32 timeoutUs);

#ifdef __cplusplus
}
#endif

#endif /* NVX_OPENRM_SHIM_H */
