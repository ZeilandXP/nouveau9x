#ifndef NVX_OPENRM_XP_NVPORT_H
#define NVX_OPENRM_XP_NVPORT_H
#include <stddef.h>
#include <nvtypes.h>
#include <nvstatus.h>
#include <nvmisc.h>
#define PORT_IS_CHECKED_BUILD 0
void *nvx_rm_alloc(NvLength bytes);
void nvx_rm_free(void *pointer);
void *nvx_rm_set(void *pointer,NvU8 value,NvLength bytes);
void nvx_rm_assert(const char *file,unsigned line);
#define portMemAllocNonPaged nvx_rm_alloc
#define portMemFree nvx_rm_free
#define portMemSet nvx_rm_set
#endif
