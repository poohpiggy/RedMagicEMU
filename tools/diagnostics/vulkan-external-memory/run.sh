#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
if [[ $# != 1 ]]; then
    echo "usage: bash run.sh /path/to/android-ndk-r28c" >&2
    exit 2
fi
ndk=$1
here=$(cd -- "$(dirname -- "$0")" && pwd)
toolchain="$ndk/toolchains/llvm/prebuilt/linux-x86_64"
headers="$toolchain/sysroot/usr/include"
clang="$toolchain/bin/aarch64-linux-android29-clang"
readelf="$toolchain/bin/llvm-readelf"
icd=${RME_VULKAN_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}
for file in "$headers/vulkan/vulkan.h" "$clang" "$readelf" "$icd"; do
    [[ -f $file ]] || { echo "missing: $file" >&2; exit 2; }
done
out=$(mktemp -d /var/tmp/rme-vulkan-external-memory.XXXXXXXX)
echo "artifacts=$out"
gcc -std=c11 -Wall -Wextra -Werror -idirafter "$headers" \
    "$here/test-capability.c" -o "$out/test-capability"
"$out/test-capability"
gcc -std=c11 -Wall -Wextra -Werror -idirafter "$headers" \
    "$here/probe.c" -o "$out/probe" -l:libvulkan.so.1
env -u VK_DRIVER_FILES VK_ICD_FILENAMES="$icd" \
    timeout -k 2 30 "$out/probe" | tee "$out/native.log"
set +e
env -u VK_DRIVER_FILES VK_ICD_FILENAMES="$out/missing-icd.json" \
    timeout -k 2 30 "$out/probe" >"$out/no-driver.log" 2>&1
status=$?
set -e
if [[ $status -ne 1 ]] || grep -q '^QUERY_COMPLETE$' "$out/no-driver.log"; then
    echo "FAIL missing-driver negative control: status=$status" >&2
    exit 1
fi
echo "MISSING_DRIVER_REJECTED status=$status"
"$clang" -std=c11 -Wall -Wextra -Werror -Wl,-z,max-page-size=16384 \
    "$here/probe.c" -o "$out/probe-android-arm64" -lvulkan
"$readelf" -h "$out/probe-android-arm64" >"$out/android-elf-header.txt"
"$readelf" -l "$out/probe-android-arm64" >"$out/android-elf-programs.txt"
"$readelf" -d "$out/probe-android-arm64" >"$out/android-elf-needed.txt"
grep -q 'AArch64' "$out/android-elf-header.txt"
grep -q '/system/bin/linker64' "$out/android-elf-programs.txt"
awk '$1 == "LOAD" { found=1; if ($NF != "0x4000") exit 1 }
     END { if (!found) exit 1 }' "$out/android-elf-programs.txt"
grep -F 'libvulkan.so' "$out/android-elf-needed.txt"
sha256sum "$out/probe-android-arm64"
echo "ANDROID_CROSS_COMPILE_OK (not run on Android)"
