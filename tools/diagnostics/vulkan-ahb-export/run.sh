#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Computer-only checks; never connects to a device.
set -euo pipefail
if [[ $# -ne 3 ]]; then
    echo 'usage: bash run.sh ANDROID_NDK VULKAN_HEADERS ANDROID_STUB_HEADERS' >&2
    exit 2
fi
ndk=$1
vk_headers=$2
android_headers=$3
here=$(cd -- "$(dirname -- "$0")" && pwd)
compiler=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang
readelf=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf
icd=${RME_VULKAN_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}
for path in "$ndk" "$vk_headers" "$android_headers" "$icd"; do
    [[ $path = /* ]] || { echo "expected absolute path: $path" >&2; exit 2; }
done
[[ -x $compiler && -x $readelf && -f $vk_headers/vulkan/vulkan.h &&
   -f $android_headers/android/hardware_buffer.h && -f $icd ]] || {
    echo 'missing compiler, headers or Vulkan ICD' >&2; exit 2;
}
out=$(mktemp -d /var/tmp/rme-vulkan-ahb-export.XXXXXXXX)
echo "artifacts=$out"
sha256sum "$here/probe.c" "$here/usage.h" "$here/verify.h" "$here/test-usage.c" \
    "$here/test-verify.c" "$here/linux-negative-ahb.c" >"$out/source-hashes.txt"
timeout -k 2 30 cc -std=c11 -Wall -Wextra -Werror "$here/test-verify.c" -o "$out/test-verify"
timeout -k 2 10 "$out/test-verify"
timeout -k 2 30 cc -std=c11 -Wall -Wextra -Werror '-D__INTRODUCED_IN(x)=' \
    -I "$android_headers" "$here/test-usage.c" -o "$out/test-usage"
timeout -k 2 10 "$out/test-usage"

# The Linux shim can only reject Android hardware buffer operations.
timeout -k 2 60 cc -std=c11 -Wall -Wextra -Werror -O2 '-D__INTRODUCED_IN(x)=' \
    -I "$vk_headers" -I "$android_headers" "$here/probe.c" "$here/linux-negative-ahb.c" \
    -Wl,-l:libvulkan.so.1 -o "$out/probe-linux-negative"
status=0
env -u VK_DRIVER_FILES -u VK_ADD_DRIVER_FILES -u VK_LAYER_PATH -u VK_INSTANCE_LAYERS \
    -u LD_PRELOAD -u LD_AUDIT VK_ICD_FILENAMES="$icd" timeout -k 2 30 "$out/probe-linux-negative" \
    >"$out/linux-negative.log" 2>&1 || status=$?
cat "$out/linux-negative.log"
if [[ $status -ne 1 ]] || ! grep -q '^AHB_BUFFER ' "$out/linux-negative.log" ||
   ! grep -q '^UNSUPPORTED AHB BLOB export for gfxstream six-usage buffer$' "$out/linux-negative.log" ||
   grep -q 'AHB_EXPORT_OK\|AHB_VULKAN_EXPORT_ROUNDTRIP_OK' "$out/linux-negative.log"; then
    echo "unexpected Linux unsupported-control result: $status" >&2
    exit 1
fi
echo 'LINUX_UNSUPPORTED_CONTROL_OK'

timeout -k 2 60 "$compiler" --target=aarch64-linux-android29 -std=c11 \
    -Wall -Wextra -Werror -O2 -Wl,-z,max-page-size=16384 \
    -Wl,-z,common-page-size=16384 -Wl,--no-undefined \
    "$here/probe.c" -lvulkan -landroid -o "$out/probe-android-arm64"
"$readelf" -h -l -d "$out/probe-android-arm64" >"$out/android-elf.txt"
grep -q 'Machine:.*AArch64' "$out/android-elf.txt"
grep -q 'Requesting program interpreter: /system/bin/linker64' "$out/android-elf.txt"
grep -q 'Shared library: \[libvulkan.so\]' "$out/android-elf.txt"
grep -q 'Shared library: \[libandroid.so\]' "$out/android-elf.txt"
awk '$1 == "LOAD" { count++; if ($NF != "0x4000") exit 1 } END { if (count < 1) exit 1 }' "$out/android-elf.txt"
sha256sum "$out/probe-android-arm64"
echo 'ANDROID_ARM64_CROSS_BUILD_OK (not run on Android)'
