#!/bin/sh
set -eu
[ "$#" -eq 3 ] || [ "$#" -eq 4 ] || { echo "usage: $0 SOURCE BUILD OUTPUT [NEWER_VK_XML]" >&2; exit 2; }
src=$1
build=$2
out=$3
g="$src/guest"
b="$build/guest"
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$out"
test_define=
remote_source=
if [ "${RME_REMOTE_SURFACE_TEST:-0}" = 1 ]; then
  test_define=-DRME_EXPERIMENTAL_REMOTE_SURFACE
  remote_source="$here/android_remote_swapchain.c"
fi
if [ "$#" -eq 4 ]; then
  python3 "$here/generate_public_api.py" "$g/mesa/src/vulkan/registry/vk.xml" \
    "$b/mesa/src/vulkan/runtime/vk_dispatch_trampolines.c" "$out/public.c" "$4"
  test_define="$test_define -DRME_TEST_PROMOTED"
else
  python3 "$here/generate_public_api.py" "$g/mesa/src/vulkan/registry/vk.xml" \
    "$b/mesa/src/vulkan/runtime/vk_dispatch_trampolines.c" "$out/public.c"
fi
cc -std=gnu11 -g -ffunction-sections -fdata-sections -DHAVE_STRUCT_TIMESPEC -DHAVE_PTHREAD -DHAVE_THRD_CREATE -DHAVE_ENDIAN_H -DVK_USE_PLATFORM_ANDROID_KHR \
  $test_define \
  -I"$here" -I"$g/vulkan" -I"$g/vulkan_enc" -I"$g/mesa/include" -I"$g/mesa/src" \
  -I"$g/mesa/src/util" -I"$g/mesa/src/vulkan/runtime" -I"$g/mesa/src/vulkan/util" \
  -I"$b/vulkan_enc" -I"$b/mesa/src/vulkan/util" -I"$b/mesa/src/vulkan/runtime" \
  -I"$src/common/vulkan/include" -Wl,--gc-sections \
  "$out/public.c" "$here/android_surface.c" "$here/android_swapchain_object.c" \
  "$here/android_swapchain_flow.c" "$here/android_swapchain_flow_android.c" \
  "$here/android_swapchain_slots.c" "$here/android_buffer_queue.c" "$here/android_native_driver.c" \
  "$here/test_dispatch.c" $remote_source -o "$out/test_dispatch"
"$out/test_dispatch"
echo 'Mesa trampoline and public ABI dispatcher test passed (CPU only)'
