#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
ndk=${ANDROID_NDK_LINUX:-/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c}
bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
include="$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include"
shader_tools="$ndk/shader-tools/linux-x86_64"
icd=${RME_CPU_VULKAN_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}
for p in "$bin/aarch64-linux-android29-clang" "$bin/llvm-readelf" "$include/vulkan/vulkan.h" "$shader_tools/glslc" "$shader_tools/spirv-val" "$icd"; do
    [ -f "$p" ] || { echo "missing: $p" >&2; exit 2; }
done
out=$(mktemp -d /var/tmp/rme-vulkanbridge-compute.XXXXXXXX)
echo "artifacts=$out"
"$shader_tools/glslc" -fshader-stage=compute --target-env=vulkan1.2 "$here/../vulkan-compute/compute.comp" -o "$out/shader.spv"
"$shader_tools/spirv-val" --target-env vulkan1.2 "$out/shader.spv"
python3 - "$out/shader.spv" "$out/shader_spv.h" <<'PY'
import struct, sys
data = open(sys.argv[1], 'rb').read()
if len(data) < 20 or len(data) % 4:
    raise SystemExit('invalid SPIR-V length')
words = struct.unpack('<%dI' % (len(data)//4), data)
if words[0] != 0x07230203:
    raise SystemExit('invalid SPIR-V magic')
with open(sys.argv[2], 'w', encoding='ascii') as f:
    f.write('#include <stdint.h>\nstatic const uint32_t shader_spv[] = {\n')
    f.write(',\n'.join('    '+', '.join('0x%08xu' % w for w in words[i:i+8]) for i in range(0,len(words),8)))
    f.write('\n};\n')
PY
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror "$here/verify64_test.c" -o "$out/verify64-test"
"$out/verify64-test"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -idirafter "$include" -I "$out" \
    "$root/native/vulkanbridge/gpu_select.c" "$root/native/vulkanbridge/gpu_device.c" \
    "$root/native/vulkanbridge/gpu_buffer.c" "$root/native/vulkanbridge/gpu_submission.c" "$root/native/vulkanbridge/gpu_compute.c" \
    "$here/probe.c" -Wl,-l:libvulkan.so.1 -o "$out/cpu-probe"
status=0
env -u VK_DRIVER_FILES -u VK_ADD_DRIVER_FILES -u VK_LAYER_PATH -u VK_INSTANCE_LAYERS \
    VK_ICD_FILENAMES="$icd" "$out/cpu-probe" >"$out/cpu.log" 2>&1 || status=$?
cat "$out/cpu.log"
[ "$status" -eq 1 ] && grep -q '^DEVICE_OPEN_FAILED status=3 select=6 ' "$out/cpu.log" && \
    ! grep -q '^VULKAN_BRIDGE_COMPUTE_OK' "$out/cpu.log" || { echo "CPU rejection failed status=$status" >&2; exit 1; }
echo 'REAL_CPU_ICD_REJECTED (no compute execution)'
"$bin/aarch64-linux-android29-clang" -std=c11 -Wall -Wextra -Werror -O2 -I "$out" \
    -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384 -Wl,--no-undefined \
    "$root/native/vulkanbridge/gpu_select.c" "$root/native/vulkanbridge/gpu_device.c" \
    "$root/native/vulkanbridge/gpu_buffer.c" "$root/native/vulkanbridge/gpu_submission.c" "$root/native/vulkanbridge/gpu_compute.c" \
    "$here/probe.c" -lvulkan -o "$out/vulkanbridge-compute-android-arm64"
"$bin/llvm-readelf" -h -l -d "$out/vulkanbridge-compute-android-arm64" > "$out/elf.txt"
grep -q 'Machine:.*AArch64' "$out/elf.txt"
grep -q 'Requesting program interpreter: /system/bin/linker64' "$out/elf.txt"
grep -q 'Shared library: \[libvulkan.so\]' "$out/elf.txt"
awk '$1 == "LOAD" { count++; if ($NF != "0x4000") exit 1 } END { if (count < 1) exit 1 }' "$out/elf.txt"
sha256sum "$out/vulkanbridge-compute-android-arm64"
echo 'ANDROID_ARM64_BUILT (not run on Android)'
