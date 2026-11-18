#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ndk=${ANDROID_NDK_LINUX:-/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c}
headers="$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include"
[ -f "$headers/vulkan/vulkan.h" ] || { echo "missing Vulkan headers: $headers" >&2; exit 2; }
out=$(mktemp -d /var/tmp/rme-host-fixed-compute-fake.XXXXXXXX)
echo "artifacts=$out"
cat > "$out/host_shader_spv.h" <<'EOF'
#include <stdint.h>
/* Only a fake loader receives this header; it must never be sent to a GPU. */
static const uint32_t host_shader_spv[] = { 0x07230203u, 0x00010000u, 0u, 1u, 0u };
EOF
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -idirafter "$headers" -I "$out" \
    "$here/../host_fixed_compute.c" "$here/host_fixed_compute_test.c" -o "$out/test"
"$out/test"
