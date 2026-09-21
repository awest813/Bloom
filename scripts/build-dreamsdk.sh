#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

export PATH="/opt/toolchains/dc/kos/utils/build_wrappers:/mingw64/bin:/usr/bin:$PATH"
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-"$repo_root/build/dreamsdk"}
if [ "$#" -gt 0 ]; then shift; fi

export DREAMSDK_HOME="${DREAMSDK_HOME:-$(cygpath -m /)}"
# KOS's environment script may reference optional unset variables.
set +u
source /opt/toolchains/dc/kos/environ.sh
set -u

# Match the option name so both -DNAME=value and -DNAME:TYPE=value keep
# Dreamcast paths unchanged. Other paths still receive MSYS conversion.
export MSYS2_ARG_CONV_EXCL="${MSYS2_ARG_CONV_EXCL:+$MSYS2_ARG_CONV_EXCL;}-DWITH_GAME_PATH;-DWITH_BIOS_PATH;-DWITH_BOOT_SSTATE;-DWITH_MCD1_PATH;-DWITH_MCD2_PATH"

cd "$repo_root"
cmake -S "$repo_root" -B "$build_dir" -G "MSYS Makefiles" \
    "-DCMAKE_TOOLCHAIN_FILE=$repo_root/cmake/dreamsdk.toolchain.cmake" "$@"
cmake --build "$build_dir" --parallel "${BLOOM_BUILD_JOBS:-4}"
"${KOS_CC_PREFIX}-objcopy" -O binary "$build_dir/bloom.elf" "$build_dir/bloom.bin"
"$KOS_BASE/utils/scramble/scramble" "$build_dir/bloom.bin" "$build_dir/1ST_READ.BIN"
