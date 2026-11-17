# Build nouveau's nvkm core + nvif client library against the lkpi shim.
#
#   make nvkm-host   ../build/host/libnvkm.a   (test harness, native cc)
#   make nvkm-w64    ../build/w64/libnvkm.a    (XP x64 miniport)
#   make nvkm-w32    ../build/w32/libnvkm.a    (XP x86 miniport / Win98 VxD)
#
# The sources come from ../nouveau, fetched by ../tools/fetch_nouveau.sh.

NOUVEAU = ../nouveau
BUILD = ../build
NVKM_SRCS := $(shell [ -d $(NOUVEAU)/nvkm ] && python3 lkpi/nvkm_sources.py $(NOUVEAU))
LKPI_SRCS = lkpi/lkpi_string.c lkpi/lkpi_core.c
NVKM_INC = -Ilkpi/include -I$(NOUVEAU)/linux-include -I$(NOUVEAU)/include \
           -I$(NOUVEAU)/include/nvkm -I$(NOUVEAU)/nvkm -I$(NOUVEAU)
# Freestanding: nvkm sees only the shim, never a C library. The -Werror
# selections are the ones that catch a shim gap or an LLP64 truncation.
NVKM_CFLAGS = -std=gnu11 -ffreestanding -fno-builtin -fno-strict-aliasing \
              -fno-stack-protector -ffunction-sections -fdata-sections -O2 \
              -Wall -Wno-unused -Wno-missing-braces -Wno-address-of-packed-member \
              -Wno-misleading-indentation -Wno-pointer-sign -Wno-maybe-uninitialized \
              -Werror=implicit-function-declaration -Werror=int-conversion \
              -Werror=incompatible-pointer-types -Werror=pointer-to-int-cast \
              -Werror=int-to-pointer-cast -Werror=format $(NVKM_INC)

# $(1) target name, $(2) compiler, $(3) extra flags, $(4) archiver
define nvkm_target
NVKM_OBJS_$(1) = $$(patsubst %.c,$(BUILD)/$(1)/%.o,$$(NVKM_SRCS)) \
                 $$(patsubst %.c,$(BUILD)/$(1)/%.o,$$(LKPI_SRCS))
$(BUILD)/$(1)/%.o: $(NOUVEAU)/%.c lkpi/include/lkpi.h
	@mkdir -p $$(@D)
	@$(2) $$(NVKM_CFLAGS) $(3) -c $$< -o $$@
$(BUILD)/$(1)/lkpi/%.o: lkpi/%.c lkpi/include/lkpi.h lkpi/include/lkpi_backend.h
	@mkdir -p $$(@D)
	$(2) $$(NVKM_CFLAGS) -Wextra -Wno-unused-parameter $(3) -c $$< -o $$@
# 'ar rcs' on an existing archive would keep stale members; build to a
# temporary name and move it into place.
$(BUILD)/$(1)/libnvkm.a: $$(NVKM_OBJS_$(1))
	@$(4) rcs $$@.tmp $$(NVKM_OBJS_$(1))
	@mv -f $$@.tmp $$@
	@echo "built $$@ ($$(words $$(NVKM_OBJS_$(1))) objects)"
nvkm-$(1): nouveau-present $(BUILD)/$(1)/libnvkm.a
endef

$(eval $(call nvkm_target,host,$(HOSTCC),-g,ar))
# Same sources under AddressSanitizer + UBSan, for the host tests. Unaligned
# access is normal in nvkm (packed firmware/VBIOS structures on x86).
SANITIZE = -fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer
$(eval $(call nvkm_target,asan,$(HOSTCC),-g -O1 $(SANITIZE),ar))
$(eval $(call nvkm_target,w64,$(CC),-m64,x86_64-w64-mingw32-ar))
$(eval $(call nvkm_target,w32,$(CC32),-m32,i686-w64-mingw32-ar))

# Host backend + tests. host_os.c is the only file that sees the C library.
HOST_BACKEND = $(BUILD)/asan/lkpi/host/lkpi_host.o $(BUILD)/asan/lkpi/host/hw_mock.o \
               $(BUILD)/asan/host_os.o
$(BUILD)/asan/host_os.o: lkpi/host/host_os.c lkpi/host/host_os.h
	@mkdir -p $(@D)
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -g $(SANITIZE) -c $< -o $@
$(BUILD)/asan/tests/%.o: tests/%.c lkpi/include/lkpi.h lkpi/host/lkpi_host.h
	@mkdir -p $(@D)
	$(HOSTCC) $(NVKM_CFLAGS) -Wextra -Wno-unused-parameter -Wno-sign-compare -g -O1 $(SANITIZE) -c $< -o $@
$(BUILD)/asan/lkpi_test: $(BUILD)/asan/tests/lkpi_test.o $(BUILD)/asan/lkpi/lkpi_string.o \
                         $(BUILD)/asan/lkpi/lkpi_core.o $(HOST_BACKEND)
	$(HOSTCC) $(SANITIZE) -o $@ $^
$(BUILD)/asan/nvkm_host_test: $(BUILD)/asan/tests/nvkm_host_test.o $(BUILD)/asan/tools/nvkm_bringup.o \
                              $(HOST_BACKEND) $(BUILD)/asan/libnvkm.a
	$(HOSTCC) $(SANITIZE) -Wl,--gc-sections -o $@ $^

# nvkm_vfio: the same nvkm + shim against a real card bound to vfio-pci on a
# Linux host. See tools/nvkm_vfio.c.
VFIO_OBJS = $(BUILD)/host/tools/nvkm_vfio.o $(BUILD)/host/tools/nvkm_bringup.o \
            $(BUILD)/host/lkpi/host/lkpi_host.o \
            $(BUILD)/host/lkpi/host/hw_vfio.o $(BUILD)/host/nvx_chip.o \
            $(BUILD)/host/host_os.o $(BUILD)/host/vfio_os.o
$(BUILD)/host/tools/%.o: tools/%.c tools/nvkm_bringup.h lkpi/include/lkpi.h lkpi/host/hw_vfio.h nvx_chip.h
	@mkdir -p $(@D)
	$(HOSTCC) $(NVKM_CFLAGS) -Wextra -Wno-unused-parameter -Wno-sign-compare -g -c $< -o $@
$(BUILD)/asan/tools/%.o: tools/%.c tools/nvkm_bringup.h lkpi/include/lkpi.h
	@mkdir -p $(@D)
	$(HOSTCC) $(NVKM_CFLAGS) -Wextra -Wno-unused-parameter -Wno-sign-compare -g -O1 $(SANITIZE) -c $< -o $@
$(BUILD)/host/nvx_chip.o: nvx_chip.c nvx_chip.h nvx_pascal_ids.h
	@mkdir -p $(@D)
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -g -O2 -c $< -o $@
$(BUILD)/host/%_os.o: lkpi/host/%_os.c lkpi/host/host_os.h lkpi/host/vfio_os.h
	@mkdir -p $(@D)
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -g -O2 -c $< -o $@
$(BUILD)/host/nvkm_vfio: $(VFIO_OBJS) $(BUILD)/host/libnvkm.a
	$(HOSTCC) -Wl,--gc-sections -o $@ $^
.PHONY: nvkm-vfio
nvkm-vfio: nouveau-present $(BUILD)/host/nvkm_vfio

.PHONY: check-nvkm
check-nvkm: nouveau-present $(BUILD)/asan/lkpi_test $(BUILD)/asan/nvkm_host_test nvkm-vfio
	$(BUILD)/asan/lkpi_test
	$(BUILD)/asan/nvkm_host_test

.PHONY: nouveau-present nvkm-host nvkm-w64 nvkm-w32 nvkm-all
nouveau-present:
	@[ -d $(NOUVEAU)/nvkm ] || { echo "run ../tools/fetch_nouveau.sh first"; exit 1; }
nvkm-all: nvkm-host nvkm-w64 nvkm-w32
