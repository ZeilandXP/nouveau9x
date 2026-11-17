.DEFAULT_GOAL := all
CC = x86_64-w64-mingw32-gcc
CC32 = i686-w64-mingw32-gcc
HOSTCC ?= cc

SOURCES = nvxmp.c nvx_log.c nvx_vbe.c nvx_chip.c nvx_swblt.c
HEADERS = miniport.h nvx_device.h nvx_log.h nvx_vbe.h nvx_chip.h nvx_swblt.h \
          nvx_pascal_ids.h nvx_3d_abi.h nvx_nvc0_address_pairs.h

COMMON = -Wall -Wextra -O2 -nostdlib -fno-builtin -fno-stack-protector \
         -fno-strict-aliasing -I.
CFLAGS = $(COMMON) -m64
CFLAGS32 = $(COMMON) -m32
LINK = -shared -nostartfiles -Wl,--image-base,0x10000 \
       -Wl,--file-alignment,0x200 -Wl,--section-alignment,0x1000
LDFLAGS = $(LINK) -Wl,--entry=DriverEntry -Wl,--subsystem,native:5.2 -L. -lvideoprt -lntoskrnl
LDFLAGS32 = $(LINK) -Wl,--entry=_DriverEntry@8 -Wl,--subsystem,native:5.1 -lvideoprt -lntoskrnl -lgcc

all: nvxmp.sys nvxmp32.sys

include nvkm.mk

nvxmp.sys: $(SOURCES) $(HEADERS) nvxmp.def libvideoprt.a
	$(CC) $(CFLAGS) -o $@ $(SOURCES) nvxmp.def $(LDFLAGS)

nvxmp32.sys: $(SOURCES) $(HEADERS)
	$(CC32) $(CFLAGS32) -o $@ $(SOURCES) $(LDFLAGS32)

libvideoprt.a: videoprt.def
	x86_64-w64-mingw32-dlltool -d videoprt.def -l $@

# The x86 build links MinGW's own libvideoprt.a (stdcall-decorated imports);
# MinGW ships none for x64, hence the generated one above.

clean:
	rm -f nvxmp.sys nvxmp32.sys libvideoprt.a
	rm -rf $(BUILD)/host $(BUILD)/asan $(BUILD)/w64 $(BUILD)/w32

HOSTFLAGS = -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
            -DNVX_HOST_TEST -I. -Itests

# 'check' needs ../nouveau (see ../tools/fetch_nouveau.sh); 'check-driver'
# covers everything that does not involve nvkm.
.PHONY: check check-driver
check: check-driver check-nvkm
check-driver: all
	python3 tools/gen_pascal_ids.py --check
	mkdir -p $(BUILD)
	$(HOSTCC) $(HOSTFLAGS) -o $(BUILD)/chip tests/chip_test.c nvx_chip.c
	$(BUILD)/chip
	$(HOSTCC) $(HOSTFLAGS) -o $(BUILD)/swblt tests/swblt_test.c nvx_swblt.c
	$(BUILD)/swblt
	$(HOSTCC) $(HOSTFLAGS) -o $(BUILD)/vbe tests/vbe_mock_test.c nvx_vbe.c
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/vbe
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -I. -o $(BUILD)/3d-abi tests/3d_abi_test.c
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/3d-abi
	python3 tools/check_imports.py nvxmp.sys nvxmp32.sys
