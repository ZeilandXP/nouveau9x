/* Isolated NVE4 P2MF BO-destination probe for the XP x64 guest. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../mesa_patches/nvx_winsys_win32.h"

#define PKT_INCR(count, subch, method) \
   (0x20000000U | ((uint32_t)(count) << 16) | \
    ((uint32_t)(subch) << 13) | ((uint32_t)(method) >> 2))
#define PKT_ONEINCR(count, subch, method) \
   (0xa0000000U | ((uint32_t)(count) << 16) | \
    ((uint32_t)(subch) << 13) | ((uint32_t)(method) >> 2))

static int
run_probe(HDC hdc, int gart)
{
   NVX_WINSYS_DEVICE device;
   NVX_3D_ALLOC_BUFFER request, target;
   NVX_3D_BATCH_HEADER header;
   NVX_3D_BUFFER_REFERENCE reference;
   uint32_t words[14];
   uint8_t batch[sizeof(header) + sizeof(reference) + sizeof(words)];
   uint32_t result[4] = {0, 0, 0, 0};
   uint64_t fence = 0;
   uint32_t n = 0;
   int submitted = 0, ok = 0;
   const uint32_t expected[4] = {
      0x4e565831U, 0x504d4632U, 0x13579bdfU, 0x2468ace0U
   };

   memset(&device, 0, sizeof(device));
   memset(&request, 0, sizeof(request));
   memset(&target, 0, sizeof(target));
   if (!NvxWinsysInitHdc(&device, hdc)) {
      puts("NVX [P2MF-PROBE]: client init failed");
      return 0;
   }
   if (!NvxWinsysOpenNouveauChannel(&device)) {
      puts("NVX [P2MF-PROBE]: Nouveau channel open failed");
      goto done;
   }
   request.Size = sizeof(request);
   request.Version = NVX_3D_ABI_VERSION;
   request.Usage = gart ? NVX_3D_USAGE_GART_BUFFER : 0;
   request.Bytes = NVX_3D_PAGE_BYTES;
   request.Alignment = NVX_3D_PAGE_BYTES;
   request.ClientToken = device.ClientToken;
   if (!NvxWinsysAllocBuffer(&device, &request, &target)) {
      puts("NVX [P2MF-PROBE]: target BO allocation failed");
      goto done;
   }

   words[n++] = PKT_INCR(1, 2, 0x0000); /* SET_OBJECT */
   words[n++] = device.NouveauObjectBindings[2].ClassEngineId;
   words[n++] = PKT_INCR(2, 2, 0x0188); /* OFFSET_OUT_UPPER/LOWER */
   words[n++] = (uint32_t)(target.GpuVa >> 32);
   words[n++] = (uint32_t)target.GpuVa;
   words[n++] = PKT_INCR(2, 2, 0x0180); /* LINE_LENGTH_IN/LINE_COUNT */
   words[n++] = sizeof(expected);
   words[n++] = 1;
   words[n++] = PKT_ONEINCR(5, 2, 0x01b0); /* LAUNCH_DMA + inline words */
   words[n++] = 0x00001001U; /* UPLOAD_EXEC; same packet as Mesa NVE4_P2MF */
   memcpy(&words[n], expected, sizeof(expected));
   n += 4;

   memset(&header, 0, sizeof(header));
   header.Size = sizeof(header);
   header.Version = NVX_3D_ABI_VERSION;
   header.DwordCount = n;
   header.BufferReferenceCount = 1;
   header.ClientToken = device.ClientToken;
   reference.BufferId = target.BufferId;
   reference.Access = NVX_3D_RELOC_WRITE;
   reference.Reserved = 0;
   memcpy(batch, &header, sizeof(header));
   memcpy(batch + sizeof(header), &reference, sizeof(reference));
   memcpy(batch + sizeof(header) + sizeof(reference), words,
          n * sizeof(words[0]));

   printf("NVX [P2MF-PROBE]: target=%s id=%lu va=%08lx%08lx bytes=%u classEngineId=%08lx\n",
          gart ? "GART" : "FB", (unsigned long)target.BufferId,
          (unsigned long)(uint32_t)(target.GpuVa >> 32),
          (unsigned long)(uint32_t)target.GpuVa, target.Bytes,
          (unsigned long)device.NouveauObjectBindings[2].ClassEngineId);
   submitted = NvxWinsysSubmitBatch(&device, batch,
                                   sizeof(header) + sizeof(reference) +
                                   n * sizeof(words[0]), &fence);
   printf("NVX [P2MF-PROBE]: submit=%s fence=%lu\n",
          submitted ? "completed" : "failed-or-timeout",
          (unsigned long)fence);
   if (submitted && NvxWinsysDownloadBuffer(&device, target.BufferId,
            target.Bytes, 0, result, sizeof(result))) {
      ok = memcmp(result, expected, sizeof(result)) == 0;
      printf("NVX [P2MF-PROBE]: readback=%s values=%08lx,%08lx,%08lx,%08lx\n",
             ok ? "match" : "mismatch", (unsigned long)result[0],
             (unsigned long)result[1], (unsigned long)result[2],
             (unsigned long)result[3]);
   } else {
      puts("NVX [P2MF-PROBE]: readback unavailable");
   }
   if (!submitted)
      goto done; /* Never try to recover or submit behind a timed-out fence. */
   if (target.BufferId && !NvxWinsysFreeBuffer(&device, target.BufferId))
      puts("NVX [P2MF-PROBE]: target BO cleanup failed");
   target.BufferId = 0;
done:
   if (submitted)
      (void)NvxWinsysCloseClient(&device);
   return ok;
}

int
main(int argc, char **argv)
{
   HWND window;
   HDC hdc;
   int gart, result;
   if (argc != 2 || (strcmp(argv[1], "gart") && strcmp(argv[1], "fb"))) {
      puts("usage: nvx_p2mf_probe.exe gart|fb");
      return 2;
   }
   gart = strcmp(argv[1], "gart") == 0;
   window = CreateWindowA("STATIC", "NVX P2MF probe", WS_POPUP,
                          0, 0, 8, 8, NULL, NULL,
                          GetModuleHandleA(NULL), NULL);
   if (!window) return 2;
   hdc = GetDC(window);
   result = hdc ? run_probe(hdc, gart) : 0;
   if (hdc) ReleaseDC(window, hdc);
   DestroyWindow(window);
   printf("[P2MF_BO_RESULT] %s target=%s\n", result ? "PASS" : "FAIL",
          gart ? "GART" : "FB");
   return result ? 0 : 1;
}
