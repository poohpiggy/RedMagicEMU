#!/bin/sh
# Fake process behavior, real CPU refusal, and Android ARM64 host-service build.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ndk=${ANDROID_NDK_LINUX:-/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c}
bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
include="$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include"
icd=${RME_CPU_VULKAN_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}
shader_tools="$ndk/shader-tools/linux-x86_64"
for p in "$bin/aarch64-linux-android29-clang" "$bin/llvm-readelf" "$include/vulkan/vulkan.h" "$icd" "$shader_tools/glslc" "$shader_tools/spirv-val"; do
    [ -f "$p" ] || { echo "missing: $p" >&2; exit 2; }
done
tmp=$(mktemp -d /var/tmp/rme-vulkan-host-service.XXXXXXXX)
echo "artifacts=$tmp"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -idirafter "$include" \
    "$here/../host_service.c" "$here/../gpu_select.c" "$here/../gpu_device.c" \
    "$here/service_fake_vulkan.c" "$here/service_fake_compute.c" -o "$tmp/service-fake"
RME_VK_SERVICE_TEST_BINARY="$tmp/service-fake" python3 "$here/service_process_test.py"
RME_VK_SERVICE_TEST_BINARY="$tmp/service-fake" python3 "$here/service_parent_death_test.py"
sh "$here/run_host_fixed_compute_test.sh"
echo 'FAKE_SERVICE_PROCESS_TEST_OK (fake GPU, no Guest)'
"$shader_tools/glslc" -fshader-stage=compute --target-env=vulkan1.2 "$here/../host_compute.comp" -o "$tmp/shader.spv"
"$shader_tools/spirv-val" --target-env vulkan1.2 "$tmp/shader.spv"
python3 - "$tmp/shader.spv" "$tmp/host_shader_spv.h" <<'PY'
import struct, sys
data = open(sys.argv[1], 'rb').read()
if len(data) < 20 or len(data) % 4: raise SystemExit('invalid SPIR-V length')
words = struct.unpack('<%dI' % (len(data)//4), data)
if words[0] != 0x07230203: raise SystemExit('invalid SPIR-V magic')
with open(sys.argv[2], 'w', encoding='ascii') as f:
    f.write('#include <stdint.h>\nstatic const uint32_t host_shader_spv[] = {\n')
    f.write(',\n'.join('    '+', '.join('0x%08xu' % w for w in words[i:i+8]) for i in range(0,len(words),8)))
    f.write('\n};\n')
PY
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -idirafter "$include" \
    -I "$tmp" "$here/../host_service.c" "$here/../host_fixed_compute.c" "$here/../gpu_select.c" "$here/../gpu_device.c" \
    "$here/../gpu_buffer.c" "$here/../gpu_submission.c" "$here/../gpu_compute.c" \
    -Wl,-l:libvulkan.so.1 -o "$tmp/service-cpu"
status=0
env -u VK_DRIVER_FILES -u VK_ADD_DRIVER_FILES -u VK_LAYER_PATH -u VK_INSTANCE_LAYERS \
    VK_ICD_FILENAMES="$icd" "$tmp/service-cpu" --parent-pid "$$" </dev/null >"$tmp/cpu.out" 2>"$tmp/cpu.err" || status=$?
cat "$tmp/cpu.err"
[ "$status" -ne 0 ] && [ ! -s "$tmp/cpu.out" ] && \
    grep -q 'RME_VK_HOST_DEVICE_FAILED status=3 select=6' "$tmp/cpu.err" || {
        echo "CPU ICD rejection check failed, exit=$status" >&2; exit 1;
    }
echo 'REAL_SOFTWARE_CPU_ICD_REJECTED'
"$bin/aarch64-linux-android29-clang" -std=c11 -Wall -Wextra -Werror -O2 -fPIE -pie \
    -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384 -Wl,--no-undefined \
    -I "$tmp" "$here/../host_service.c" "$here/../host_fixed_compute.c" "$here/../gpu_select.c" "$here/../gpu_device.c" \
    "$here/../gpu_buffer.c" "$here/../gpu_submission.c" "$here/../gpu_compute.c" \
    -lvulkan -o "$tmp/vulkan-host-service-arm64"
"$bin/llvm-readelf" -h -l -d "$tmp/vulkan-host-service-arm64" >"$tmp/elf.txt"
grep -q 'Machine:.*AArch64' "$tmp/elf.txt"
grep -q 'Requesting program interpreter: /system/bin/linker64' "$tmp/elf.txt"
grep -q 'Shared library: \[libvulkan.so\]' "$tmp/elf.txt"
awk '$1 == "LOAD" { count++; if ($NF != "0x4000") exit 1 } END { if (count < 1) exit 1 }' "$tmp/elf.txt"
sha256sum "$tmp/vulkan-host-service-arm64"
echo 'ANDROID_ARM64_HOST_SERVICE_BUILT (not run on Android; no Guest)'
