#ifndef NVX_LOG_H
#define NVX_LOG_H
/* LogStr/LogHex are declared in nvx_device.h. */
VOID NvxLogFlush(VOID);
ULONG NvxLogRead(PUCHAR out, ULONG maxBytes);
#endif
