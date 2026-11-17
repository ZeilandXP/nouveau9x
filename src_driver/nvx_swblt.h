/*
 * Software backend for the NVX 2D ABI (fill / copy / upload).
 *
 * The display driver keeps GDI's pixels in system memory and presents them
 * through three miniport operations addressed by scanout offset. Until the
 * Pascal copy engine is brought up these run on the CPU against the mapped
 * scanout. Validation is shared with the future hardware backend, and the
 * whole file is OS-independent so it is exercised by host tests.
 */
#ifndef NVX_SWBLT_H
#define NVX_SWBLT_H
#include <stdint.h>
#include <stddef.h>

/* Largest single operation accepted from the display driver (16 MiB). */
#define NVX_BLT_MAX_BYTES 0x01000000U

typedef struct {
    uint8_t *Base;      /* CPU mapping of the scanout surface */
    uint64_t Bytes;     /* stride * height */
    uint64_t GpuVa;     /* address the display driver uses for offset 0 */
} NVX_SURFACE;

typedef enum {
    NVX_BLT_OK = 0,
    NVX_BLT_UNSUPPORTED,    /* shape this backend does not handle */
    NVX_BLT_INVALID         /* out of range, misaligned, or overlapping */
} NVX_BLT_STATUS;

NVX_BLT_STATUS NvxSwFill(const NVX_SURFACE *s, uint64_t dstVa, uint32_t color,
                         uint32_t bytes);
NVX_BLT_STATUS NvxSwCopy(const NVX_SURFACE *s, uint64_t dstVa, uint64_t srcVa,
                         uint32_t bytesPerLine, uint32_t srcPitch,
                         uint32_t dstPitch, uint32_t lineCount);
NVX_BLT_STATUS NvxSwUpload(const NVX_SURFACE *s, uint64_t dstVa,
                           const uint8_t *data, uint32_t bytes);

#endif
