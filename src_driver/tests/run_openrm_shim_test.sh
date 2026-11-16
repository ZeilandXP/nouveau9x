#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d /tmp/nvx-openrm-test.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT
python3 tools/import_openrm_policy.py ../open-gpu-kernel-modules "$build_dir/nvx_openrm_policy_generated.c"
cp -r include openrm_xp nvx_openrm_policy.c nvx_openrm_policy_view.h nvx_openrm_containers.c nvx_3d_abi.h nvx_nvc0_address_pairs.h nvx_display.h nvx_gr_context.h nvx_gsp.h nvx_openrm_shim.h nvx_openrm_shim.c tests/openrm_shim_test.c "$build_dir/"
cp tests/host_videoport.h "$build_dir/miniport.h"
cp tests/host_videoport.h "$build_dir/host_videoport.h"
${HOSTCC:-cc} -std=c11 -DNVX_OPENRM_HOSTED=1 -Wall -Wextra -Werror -g -fsanitize=address,undefined -I"$build_dir/openrm_xp" -I$(pwd)/../open-gpu-kernel-modules/src/nvidia/inc -I$(pwd)/../open-gpu-kernel-modules/src/nvidia/inc/libraries -I"$build_dir" -I"$build_dir/include" -I$(pwd)/../open-gpu-kernel-modules/src/common/sdk/nvidia/inc -I$(pwd)/../open-gpu-kernel-modules/src/common/inc -I$(pwd)/../open-gpu-kernel-modules/src/nvidia/arch/nvalloc/common/inc -I$(pwd)/../open-gpu-kernel-modules/src/common/shared/msgq/inc -o "$build_dir/test" "$build_dir/openrm_shim_test.c" "$build_dir/nvx_openrm_shim.c" "$build_dir/nvx_openrm_policy.c" "$build_dir/nvx_openrm_policy_generated.c" "$build_dir/nvx_openrm_containers.c" "$(pwd)/../open-gpu-kernel-modules/src/nvidia/src/libraries/containers/eheap/eheap_old.c" "$(pwd)/../open-gpu-kernel-modules/src/nvidia/src/libraries/containers/btree/btree.c"
ASAN_OPTIONS=detect_leaks=0 "$build_dir/test"
