#include "nvx_swblt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define W 64
#define H 16
#define STRIDE (W * 4)
#define VA 0x20000000ULL

static uint32_t guarded[W * H + 2 * W];
static uint32_t *const fb = guarded + W;

static void reset(void)
{
    for (size_t i = 0; i < sizeof(guarded) / 4; i++) guarded[i] = 0xdeadbeef;
    for (int i = 0; i < W * H; i++) fb[i] = (uint32_t)i;
}
static void guards_intact(void)
{
    for (int i = 0; i < W; i++)
        assert(guarded[i] == 0xdeadbeef && guarded[W + W * H + i] == 0xdeadbeef);
}

int main(void)
{
    NVX_SURFACE s = { (uint8_t *)fb, (uint64_t)STRIDE * H, VA };
    uint8_t payload[STRIDE];

    /* Fill: one row segment, exact bounds, nothing else touched. */
    reset();
    assert(NvxSwFill(&s, VA + 2 * STRIDE + 8, 0x00112233, 16) == NVX_BLT_OK);
    for (int i = 0; i < W * H; i++)
        assert(fb[i] == ((i >= 2 * W + 2 && i < 2 * W + 6) ? 0x00112233u : (uint32_t)i));
    /* Fill to the very last pixel is legal; one byte further is not. */
    assert(NvxSwFill(&s, VA + STRIDE * H - 4, 1, 4) == NVX_BLT_OK);
    assert(NvxSwFill(&s, VA + STRIDE * H - 4, 1, 8) == NVX_BLT_INVALID);
    assert(NvxSwFill(&s, VA + STRIDE * H, 1, 4) == NVX_BLT_INVALID);
    assert(NvxSwFill(&s, VA - 4, 1, 4) == NVX_BLT_INVALID);
    assert(NvxSwFill(&s, VA + 2, 1, 4) == NVX_BLT_INVALID);       /* misaligned */
    assert(NvxSwFill(&s, VA, 1, 0) == NVX_BLT_UNSUPPORTED);
    assert(NvxSwFill(&s, VA, 1, 6) == NVX_BLT_UNSUPPORTED);
    assert(NvxSwFill(&s, VA, 1, NVX_BLT_MAX_BYTES + 4) == NVX_BLT_UNSUPPORTED);
    /* A wrapped address must not pass the range check. */
    assert(NvxSwFill(&s, 0xfffffffffffffffcULL, 1, 4) == NVX_BLT_INVALID);
    guards_intact();

    /* Copy: a 4x3 pixel block between disjoint rectangles. */
    reset();
    assert(NvxSwCopy(&s, VA + 8 * STRIDE + 40, VA + 1 * STRIDE + 4, 16, STRIDE, STRIDE, 3) == NVX_BLT_OK);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        uint32_t expect = (uint32_t)(y * W + x);
        if (y >= 8 && y < 11 && x >= 10 && x < 14)
            expect = (uint32_t)((y - 7) * W + (x - 9));
        assert(fb[y * W + x] == expect);
    }
    /* Overlap is refused and leaves the surface untouched. */
    reset();
    assert(NvxSwCopy(&s, VA + STRIDE, VA, 16, STRIDE, STRIDE, 3) == NVX_BLT_INVALID);
    for (int i = 0; i < W * H; i++) assert(fb[i] == (uint32_t)i);
    /* Source or destination running off the end. */
    assert(NvxSwCopy(&s, VA, VA + (H - 2) * STRIDE, 16, STRIDE, STRIDE, 3) == NVX_BLT_INVALID);
    assert(NvxSwCopy(&s, VA + (H - 2) * STRIDE, VA, 16, STRIDE, STRIDE, 3) == NVX_BLT_INVALID);
    assert(NvxSwCopy(&s, VA + 8 * STRIDE, VA, 16, 8, STRIDE, 2) == NVX_BLT_UNSUPPORTED); /* pitch < row */
    assert(NvxSwCopy(&s, VA + 8 * STRIDE, VA, 16, STRIDE, STRIDE, 0) == NVX_BLT_UNSUPPORTED);
    assert(NvxSwCopy(&s, VA + 8 * STRIDE, VA, 16, STRIDE, STRIDE, 0x10001) == NVX_BLT_INVALID);
    guards_intact();

    /* Upload: whole row, then an unaligned ragged span through the byte path. */
    reset();
    for (int i = 0; i < STRIDE; i++) payload[i] = (uint8_t)(i * 7 + 1);
    assert(NvxSwUpload(&s, VA + 5 * STRIDE, payload, STRIDE) == NVX_BLT_OK);
    assert(!memcmp(fb + 5 * W, payload, STRIDE));
    assert(fb[5 * W - 1] == (uint32_t)(5 * W - 1) && fb[6 * W] == (uint32_t)(6 * W));
    reset();
    assert(NvxSwUpload(&s, VA + 3, payload + 1, 6) == NVX_BLT_OK);
    assert(!memcmp((uint8_t *)fb + 3, payload + 1, 6));
    assert(((uint8_t *)fb)[2] == 0 && ((uint8_t *)fb)[9] == 0); /* fb[0]=0, fb[2]=2 → bytes stay */
    assert(NvxSwUpload(&s, VA + STRIDE * H - 4, payload, 8) == NVX_BLT_INVALID);
    assert(NvxSwUpload(&s, VA, NULL, 4) == NVX_BLT_UNSUPPORTED);
    assert(NvxSwUpload(&s, VA, payload, 0) == NVX_BLT_UNSUPPORTED);
    guards_intact();

    /* No surface: everything is refused. */
    {
        NVX_SURFACE none = { NULL, 0, VA };
        assert(NvxSwFill(&none, VA, 1, 4) == NVX_BLT_INVALID);
        assert(NvxSwFill(NULL, VA, 1, 4) == NVX_BLT_INVALID);
    }
    puts("swblt_test: ok");
    return 0;
}
