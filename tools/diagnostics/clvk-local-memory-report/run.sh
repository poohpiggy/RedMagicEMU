#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -u
if [[ $# -ne 5 ]]; then
    echo "usage: bash run.sh CLVK_LIB_DIR CLSPV OPENCL_HEADERS DEP_LIB_DIRS VULKAN_ICD" >&2
    exit 2
fi
lib=$1
clspv=$2
headers=$3
deps=$4
icd=$5
here=$(cd -- "$(dirname -- "$0")" && pwd)
for path in "$lib/libOpenCL.so" "$lib/libOpenCL.so.1" "$clspv" "$headers/CL/cl.h" "$icd"; do
    [[ $path = /* && -f $path ]] || { echo "invalid absolute path: $path" >&2; exit 2; }
done
out=$(mktemp -d /var/tmp/rme-clvk-local-report-test.XXXXXXXX) || exit 2
echo "artifacts=$out"
timeout -k 2 30 cc -std=c11 -Wall -Wextra -Werror -O2 -I "$headers" \
    "$here/probe.c" -L "$lib" -lOpenCL -o "$out/probe" || exit 2
export LD_LIBRARY_PATH="$lib:$deps"
export CLVK_CLSPV_PATH="$clspv" CLVK_CACHE_DIR= CLVK_COMPILER_TEMP_DIR="$out"
export TMPDIR="$out" XDG_CACHE_HOME="$out/cache" MESA_SHADER_CACHE_DISABLE=true
export VK_ICD_FILENAMES="$icd"
unset VK_DRIVER_FILES VK_ADD_DRIVER_FILES VK_LAYER_PATH VK_INSTANCE_LAYERS LD_PRELOAD LD_AUDIT
timeout -k 2 60 "$out/probe" >"$out/probe.log" 2>&1
status=$?
cat "$out/probe.log"
echo "probe_exit=$status"
exit "$status"
