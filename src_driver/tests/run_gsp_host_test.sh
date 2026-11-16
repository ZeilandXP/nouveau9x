#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d /tmp/nvx-gsp-test.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT
python3 tools/import_openrm_policy.py ../open-gpu-kernel-modules "$build_dir/nvx_openrm_policy_generated.c"
cp -r include nvx_openrm_policy.c nvx_openrm_policy_view.h openrm_xp nvx_openrm_containers.c nvx_3d_abi.h nvx_nvc0_address_pairs.h nvx_display.h nvx_mmu.c nvx_mmu.h nvx_gr_context.c nvx_gr_context.h nvx_gsp.c nvx_gr_clients.c nvx_ce.c nvx_disp_rpc.c nvx_gsp_boot.c nvx_fw.c nvx_fw.h nvx_rpc.c nvx_rpc.h nvx_seq.c nvx_seq.h nvx_openrm_shim.h nvx_openrm_shim.c bootloader_blob.h booter_load_blob.h tests/gsp_mock_test.c tests/host_videoport.h "$build_dir/"
cp tests/host_videoport.h "$build_dir/miniport.h"
sed 's/#include "miniport.h"/#include "host_videoport.h"/' nvx_gsp.h > "$build_dir/nvx_gsp.h"
# Preserve firmware linker symbol names used by the production image.
ld -r -b binary ../dist_full/gsp_ga104.bin -o "$build_dir/blob.o"
objcopy --redefine-sym _binary____dist_full_gsp_ga104_bin_start=_binary_dist_full_gsp_ga104_bin_start --redefine-sym _binary____dist_full_gsp_ga104_bin_end=_binary_dist_full_gsp_ga104_bin_end "$build_dir/blob.o"
${HOSTCC:-cc} -std=c11 -DNVX_OPENRM_HOSTED=1 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Wl,-z,noexecstack -I"$build_dir/openrm_xp" -I$(pwd)/../open-gpu-kernel-modules/src/nvidia/inc -I$(pwd)/../open-gpu-kernel-modules/src/nvidia/inc/libraries -I"$build_dir" -I"$build_dir/include" -I$(pwd)/../open-gpu-kernel-modules/src/common/sdk/nvidia/inc -I$(pwd)/../open-gpu-kernel-modules/src/common/inc -I$(pwd)/../open-gpu-kernel-modules/src/nvidia/arch/nvalloc/common/inc -I$(pwd)/../open-gpu-kernel-modules/src/common/shared/msgq/inc -o "$build_dir/test" "$build_dir/gsp_mock_test.c" "$build_dir/nvx_gsp.c" "$build_dir/nvx_gr_context.c" "$build_dir/nvx_mmu.c" "$build_dir/nvx_fw.c" "$build_dir/nvx_rpc.c" "$build_dir/nvx_seq.c" "$build_dir/nvx_openrm_shim.c" "$build_dir/nvx_openrm_policy.c" "$build_dir/nvx_openrm_policy_generated.c" "$build_dir/nvx_openrm_containers.c" "$(pwd)/../open-gpu-kernel-modules/src/nvidia/src/libraries/containers/eheap/eheap_old.c" "$(pwd)/../open-gpu-kernel-modules/src/nvidia/src/libraries/containers/btree/btree.c" "$(pwd)/../open-gpu-kernel-modules/src/common/shared/msgq/msgq.c" "$build_dir/blob.o"
ASAN_OPTIONS=detect_leaks=0 "$build_dir/test"
