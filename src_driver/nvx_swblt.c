#include "nvx_swblt.h"

/* The miniport is built freestanding (-nostdlib -fno-builtin), so the loops
 * are written out rather than calling memcpy/memset. Writes go to a
 * framebuffer BAR, where 32-bit stores are the natural unit. */

static int NvxSurfaceOffset(const NVX_SURFACE *s, uint64_t va, uint64_t span,
                            uint64_t *offset)
{
    if (!s || !s->Base || !s->Bytes || va < s->GpuVa)
        return 0;
    *offset = va - s->GpuVa;
    if (*offset > s->Bytes || span > s->Bytes - *offset)
        return 0;
    return 1;
}

NVX_BLT_STATUS NvxSwFill(const NVX_SURFACE *s, uint64_t dstVa, uint32_t color,
                         uint32_t bytes)
{
    uint64_t offset;
    volatile uint32_t *dst;
    uint32_t i;
    if (!bytes || bytes > NVX_BLT_MAX_BYTES || (bytes & 3))
        return NVX_BLT_UNSUPPORTED;
    if (!NvxSurfaceOffset(s, dstVa, bytes, &offset) || (offset & 3))
        return NVX_BLT_INVALID;
    dst = (volatile uint32_t *)(s->Base + offset);
    for (i = 0; i < bytes / 4; i++)
        dst[i] = color;
    return NVX_BLT_OK;
}

NVX_BLT_STATUS NvxSwCopy(const NVX_SURFACE *s, uint64_t dstVa, uint64_t srcVa,
                         uint32_t bytesPerLine, uint32_t srcPitch,
                         uint32_t dstPitch, uint32_t lineCount)
{
    uint64_t srcSpan, dstSpan, srcOffset, dstOffset;
    uint32_t y, x;
    if (!bytesPerLine || bytesPerLine > NVX_BLT_MAX_BYTES || !lineCount ||
        !srcPitch || !dstPitch || srcPitch < bytesPerLine ||
        dstPitch < bytesPerLine || (bytesPerLine & 3) || (srcPitch & 3) ||
        (dstPitch & 3))
        return NVX_BLT_UNSUPPORTED;
    if (lineCount > 0x10000U)
        return NVX_BLT_INVALID;
    srcSpan = (uint64_t)(lineCount - 1) * srcPitch + bytesPerLine;
    dstSpan = (uint64_t)(lineCount - 1) * dstPitch + bytesPerLine;
    if (srcSpan > NVX_BLT_MAX_BYTES || dstSpan > NVX_BLT_MAX_BYTES ||
        !NvxSurfaceOffset(s, srcVa, srcSpan, &srcOffset) ||
        !NvxSurfaceOffset(s, dstVa, dstSpan, &dstOffset) ||
        (srcOffset & 3) || (dstOffset & 3))
        return NVX_BLT_INVALID;
    /* Same contract as the hardware path: overlapping spans are refused and
     * the display driver falls back to GDI for them. */
    if (srcOffset < dstOffset + dstSpan && dstOffset < srcOffset + srcSpan)
        return NVX_BLT_INVALID;
    for (y = 0; y < lineCount; y++) {
        const volatile uint32_t *src = (const volatile uint32_t *)
            (s->Base + srcOffset + (uint64_t)y * srcPitch);
        volatile uint32_t *dst = (volatile uint32_t *)
            (s->Base + dstOffset + (uint64_t)y * dstPitch);
        for (x = 0; x < bytesPerLine / 4; x++)
            dst[x] = src[x];
    }
    return NVX_BLT_OK;
}

NVX_BLT_STATUS NvxSwUpload(const NVX_SURFACE *s, uint64_t dstVa,
                           const uint8_t *data, uint32_t bytes)
{
    uint64_t offset;
    volatile uint8_t *dst;
    uint32_t i = 0;
    if (!data || !bytes || bytes > NVX_BLT_MAX_BYTES)
        return NVX_BLT_UNSUPPORTED;
    if (!NvxSurfaceOffset(s, dstVa, bytes, &offset))
        return NVX_BLT_INVALID;
    dst = s->Base + offset;
    /* Word stores where both sides allow it; bytes for any ragged ends. */
    if (!((uintptr_t)dst & 3) && !((uintptr_t)data & 3)) {
        for (; i + 4 <= bytes; i += 4)
            *(volatile uint32_t *)(dst + i) = *(const uint32_t *)(data + i);
    }
    for (; i < bytes; i++)
        dst[i] = data[i];
    return NVX_BLT_OK;
}
