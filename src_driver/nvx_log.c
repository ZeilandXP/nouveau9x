/*
 * Diagnostic logging for the miniport: COM1, VideoPort debug output, and a
 * resident ring that is flushed to C:\WINDOWS\nvxmp.log from PASSIVE_LEVEL
 * and can be drained by the display driver through IOCTL_NVX_GET_LOG.
 */
#include "nvx_device.h"
#include "nvx_log.h"

/* NT kernel file APIs used by the optional boot diagnostic log. */
typedef struct _NVX_OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    struct _NVX_UNICODE_STRING *ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} NVX_OBJECT_ATTRIBUTES, *PNVX_OBJECT_ATTRIBUTES;

typedef struct _NVX_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} NVX_UNICODE_STRING;

typedef struct _NVX_IO_STATUS_BLOCK {
    union { LONG Status; PVOID Pointer; } u;
    ULONG_PTR Information;
} NVX_IO_STATUS_BLOCK, *PNVX_IO_STATUS_BLOCK;

extern LONG NTAPI ZwCreateFile(PHANDLE, ACCESS_MASK, PNVX_OBJECT_ATTRIBUTES,
    PNVX_IO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
extern LONG NTAPI ZwWriteFile(HANDLE, HANDLE, PVOID, PVOID, PNVX_IO_STATUS_BLOCK,
    PVOID, ULONG, PLARGE_INTEGER, PULONG);
#if defined(__i386__)
static inline UCHAR NTAPI KeGetCurrentIrql(VOID) {
    UCHAR irql;
    __asm__ __volatile__("movb %%fs:0x24, %0" : "=q"(irql));
    return irql;
}
#else
extern UCHAR NTAPI KeGetCurrentIrql(VOID);
#endif

#ifndef __VXDKERNEL__
#define NVX_LOG_RING_BYTES 1048576UL
#define NVX_LOG_FLUSH_CHUNK 1024UL
#define NVX_LOG_PASSIVE_LEVEL 0
static HANDLE g_LogFile;
static UCHAR g_LogRing[NVX_LOG_RING_BYTES];
static volatile ULONG64 g_LogWriteSequence;
static ULONG64 g_LogReadSequence;
static ULONG g_LogOpenRetry;
static BOOLEAN g_LogMarkerWritten;
static volatile LONG g_LogFlushBusy;

static VOID NvxLogAppend(const CHAR *text, ULONG length)
{
    ULONG i;
    for (i = 0; i < length; ++i) {
        ULONG64 sequence=__sync_fetch_and_add(&g_LogWriteSequence,1);
        g_LogRing[sequence % NVX_LOG_RING_BYTES]=(UCHAR)text[i];
    }
}

/* File I/O is only attempted from PASSIVE_LEVEL. LogStr itself is also used
 * by callbacks that may run at elevated IRQL, so every message first enters
 * the resident ring and disk flushing is opportunistic. */
VOID NvxLogFlush(VOID)
{
    static WCHAR pathBuffer[] = L"\\??\\C:\\WINDOWS\\nvxmp.log";
    static const CHAR marker[] = "\r\n===== NVX diagnostic run =====\r\n";
    static const CHAR overflowPrefix[] = "NVX [LOG]: ring overflow; dropped bytes=";
    NVX_UNICODE_STRING path;
    NVX_OBJECT_ATTRIBUTES attributes;
    NVX_IO_STATUS_BLOCK io;
    LARGE_INTEGER append;
    CHAR chunk[NVX_LOG_FLUSH_CHUNK];
    ULONG count, dropped, i;
    ULONG64 end;
    LONG status;

    if (KeGetCurrentIrql() != NVX_LOG_PASSIVE_LEVEL ||
        __sync_lock_test_and_set(&g_LogFlushBusy, 1))
        return;

    if (!g_LogFile) {
        if (g_LogOpenRetry) {
            --g_LogOpenRetry;
            __sync_lock_release(&g_LogFlushBusy);
            return;
        }
        path.Buffer = pathBuffer;
        path.Length = (USHORT)(sizeof(pathBuffer) - sizeof(WCHAR));
        path.MaximumLength = sizeof(pathBuffer);
        attributes.Length = sizeof(attributes);
        attributes.RootDirectory = NULL;
        attributes.ObjectName = &path;
        attributes.Attributes = 0x40; /* OBJ_CASE_INSENSITIVE */
        attributes.SecurityDescriptor = NULL;
        attributes.SecurityQualityOfService = NULL;
        status = ZwCreateFile(&g_LogFile, 0x00100004, &attributes, &io, NULL,
            0x80, 0x00000007, 0x00000003, 0x00000020, NULL, 0);
        if (status < 0) {
            g_LogFile = NULL;
            g_LogOpenRetry = 31; /* retry after 32 safe flush opportunities */
            __sync_lock_release(&g_LogFlushBusy);
            return;
        }
    }

    append.QuadPart = -1;
    if (!g_LogMarkerWritten) {
        status = ZwWriteFile(g_LogFile, NULL, NULL, NULL, &io, (PVOID)marker,
            sizeof(marker) - 1, &append, NULL);
        if (status < 0) {
            __sync_lock_release(&g_LogFlushBusy);
            return;
        }
        g_LogMarkerWritten = TRUE;
    }

    end=__sync_fetch_and_add(&g_LogWriteSequence,0);
    dropped=0;
    if (end-g_LogReadSequence > NVX_LOG_RING_BYTES) {
        dropped=(ULONG)((end-g_LogReadSequence)-NVX_LOG_RING_BYTES);
        g_LogReadSequence=end-NVX_LOG_RING_BYTES;
    }
    if (dropped) {
        CHAR line[sizeof(overflowPrefix) + 20];
        ULONG at = sizeof(overflowPrefix) - 1;
        for (i = 0; i < at; ++i) line[i] = overflowPrefix[i];
        line[at++] = '0'; line[at++] = 'x';
        for (i = 0; i < 16; ++i) {
            ULONG shift = (15 - i) * 4;
            ULONG nibble = (dropped >> shift) & 0xFUL;
            line[at++] = (CHAR)(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        }
        line[at++] = '\r'; line[at++] = '\n';
        status = ZwWriteFile(g_LogFile, NULL, NULL, NULL, &io, line, at,
            &append, NULL);
        if (status < 0) {
            __sync_lock_release(&g_LogFlushBusy);
            return;
        }
    }

    for (;;) {
        end=__sync_fetch_and_add(&g_LogWriteSequence,0);
        count=(ULONG)(end-g_LogReadSequence);
        if (count > sizeof(chunk)) count = sizeof(chunk);
        for (i = 0; i < count; ++i)
            chunk[i] = (CHAR)g_LogRing[(g_LogReadSequence + i) % NVX_LOG_RING_BYTES];
        if (!count) break;

        status = ZwWriteFile(g_LogFile, NULL, NULL, NULL, &io, chunk, count,
            &append, NULL);
        if (status < 0 || io.Information != count) break;

        g_LogReadSequence+=count;
    }
    __sync_lock_release(&g_LogFlushBusy);
}
#else
static VOID NvxLogAppend(const CHAR *text, ULONG length) { (void)text; (void)length; }
VOID NvxLogFlush(VOID) {}
#endif

/* COM1 Direct Serial Logging (I/O Port 0x3F8) for QEMU VM */
static inline unsigned char inb(unsigned short port) {
    unsigned char val;
    __asm__ __volatile__("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void NvxLogSerial(const char *s, ULONG length) {
    ULONG i;
    for (i = 0; i < length; ++i) {
        if (s[i] == '\n') {
            for (volatile ULONG to = 0; (inb(0x3FD) & 0x20) == 0 && to < 100000; to++);
            outb(0x3F8, '\r');
        }
        for (volatile ULONG to = 0; (inb(0x3FD) & 0x20) == 0 && to < 100000; to++);
        outb(0x3F8, (unsigned char)s[i]);
    }
}

void LogStr(const char *s) {
    ULONG length = (ULONG)__builtin_strlen(s);
    NvxLogSerial(s, length);
    NvxLogAppend(s, length);
    /* VideoPortDebugPrint remains available for debugger capture. */
    VideoPortDebugPrint(Info, "%s", s);
    if (length && s[length - 1] == '\n') NvxLogFlush();
}

void LogHex(unsigned long long val) {
    char buf[19];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 15; i >= 0; i--) {
        int nibble = val & 0xF;
        buf[2 + i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        val >>= 4;
    }
    buf[18] = '\0';
    LogStr(buf);
}

/* Drain up to 'maxBytes' of unread ring content for IOCTL_NVX_GET_LOG. */
ULONG NvxLogRead(PUCHAR out, ULONG maxBytes)
{
#ifndef __VXDKERNEL__
    ULONG count, i;
    ULONG64 end = __sync_fetch_and_add(&g_LogWriteSequence, 0);
    if (end - g_LogReadSequence > NVX_LOG_RING_BYTES)
        g_LogReadSequence = end - NVX_LOG_RING_BYTES;
    count = (ULONG)(end - g_LogReadSequence);
    if (count > maxBytes) count = maxBytes;
    for (i = 0; i < count; i++)
        out[i] = g_LogRing[(g_LogReadSequence + i) % NVX_LOG_RING_BYTES];
    g_LogReadSequence += count;
    return count;
#else
    (void)out; (void)maxBytes;
    return 0;
#endif
}
