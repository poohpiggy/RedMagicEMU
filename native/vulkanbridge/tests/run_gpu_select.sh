#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ndk=${ANDROID_NDK_LINUX:-/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c}
bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
include="$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include"
icd=${RME_CPU_VULKAN_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}
for p in "$bin/aarch64-linux-android29-clang" "$bin/llvm-readelf" "$include/vulkan/vulkan.h" "$icd"; do
    [ -f "$p" ] || { echo "missing: $p" >&2; exit 2; }
done
tmp=$(mktemp -d /var/tmp/rme-vulkan-gpu-select.XXXXXXXX)
echo "artifacts=$tmp"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -idirafter "$include" \
    "$here/../gpu_select.c" "$here/gpu_select_test.c" -o "$tmp/fake-test"
"$tmp/fake-test"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -idirafter "$include" \
    "$here/../gpu_select.c" "$here/../gpu_device.c" "$here/gpu_device_test.c" -o "$tmp/fake-device-test"
"$tmp/fake-device-test"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -idirafter "$include" \
    "$here/../gpu_select.c" "$here/../gpu_device.c" "$here/gpu_features_test.c" -o "$tmp/fake-device-features-test"
"$tmp/fake-device-features-test"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -idirafter "$include" \
    "$here/../gpu_select.c" "$here/../gpu_device.c" "$here/../gpu_buffer.c" "$here/gpu_buffer_test.c" -o "$tmp/fake-buffer-test"
"$tmp/fake-buffer-test"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -idirafter "$include" \
    "$here/../gpu_select.c" "$here/../gpu_device.c" "$here/../gpu_submission.c" "$here/gpu_submission_test.c" -o "$tmp/fake-submission-test"
"$tmp/fake-submission-test"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -idirafter "$include" \
    "$here/../gpu_select.c" "$here/../gpu_device.c" "$here/../gpu_buffer.c" "$here/../gpu_submission.c" \
    "$here/../gpu_compute.c" "$here/gpu_compute_test.c" -o "$tmp/fake-compute-test"
"$tmp/fake-compute-test"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -idirafter "$include" \
    "$here/../gpu_select.c" "$here/../gpu_device.c" "$here/../gpu_buffer.c" "$here/../gpu_submission.c" "$here/gpu_select_probe.c" -Wl,-l:libvulkan.so.1 -o "$tmp/cpu-probe"
status=0
env -u VK_DRIVER_FILES -u VK_ADD_DRIVER_FILES -u VK_LAYER_PATH -u VK_INSTANCE_LAYERS \
    VK_ICD_FILENAMES="$icd" "$tmp/cpu-probe" >"$tmp/cpu.log" 2>&1 || status=$?
cat "$tmp/cpu.log"
[ "$status" -eq 1 ] && grep -Eq '^REPORTED_DEVICE .*type=4 ' "$tmp/cpu.log" && \
    grep -q '^SELECT_FAILED status=6 ' "$tmp/cpu.log" && \
    ! grep -q '^SELECTED_REPORTED_GPU' "$tmp/cpu.log" || {
        echo "CPU ICD rejection check failed, exit=$status" >&2; exit 1;
    }
echo 'REAL_SOFTWARE_CPU_ICD_REJECTED'
"$bin/aarch64-linux-android29-clang" -std=c11 -Wall -Wextra -Werror -O2 \
    -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384 -Wl,--no-undefined \
    "$here/../gpu_select.c" "$here/../gpu_device.c" "$here/../gpu_buffer.c" "$here/../gpu_submission.c" "$here/gpu_select_probe.c" -lvulkan -o "$tmp/gpu-select-android-arm64"
"$bin/llvm-readelf" -h -l -d "$tmp/gpu-select-android-arm64" >"$tmp/elf.txt"
grep -q 'Machine:.*AArch64' "$tmp/elf.txt"
grep -q 'Requesting program interpreter: /system/bin/linker64' "$tmp/elf.txt"
grep -q 'Shared library: \[libvulkan.so\]' "$tmp/elf.txt"
awk '$1 == "LOAD" { count++; if ($NF != "0x4000") exit 1 } END { if (count < 1) exit 1 }' "$tmp/elf.txt"
sha256sum "$tmp/gpu-select-android-arm64"
echo 'ANDROID_ARM64_GPU_SELECT_PROBE_BUILT (not run on Android)'
