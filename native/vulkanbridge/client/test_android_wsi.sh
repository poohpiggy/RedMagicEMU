#!/bin/sh
# CPU-only callbacks with synthetic handles/fds; no Android device or GPU.
set -eu
[ "$#" -eq 3 ] || { echo "usage: $0 SOURCE GENERATED_BUILD OUTPUT" >&2; exit 2; }
src=$1
build=$2
out=$3
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
g="$src/guest"
b="$build/guest"
mkdir -p "$out"
for name in android_surface android_buffer_queue android_swapchain_slots; do
  cc -std=c11 -Wall -Wextra -Werror -I"$g/mesa/include" \
    "$here/test_$name.c" "$here/$name.c" -o "$out/$name-test"
  "$out/$name-test"
done
cc -std=c11 -Wall -Wextra -Werror -DRME_SURFACE_REMOTE_IMPLEMENTED -I"$g/mesa/include" \
  "$here/test_android_surface_remote.c" "$here/android_surface.c" -o "$out/android-surface-remote-test"
"$out/android-surface-remote-test"
cc -std=gnu11 -Wall -Wextra -Werror -DVK_USE_PLATFORM_ANDROID_KHR \
  -DHAVE_ENDIAN_H -DHAVE_STRUCT_TIMESPEC -DHAVE_PTHREAD -DHAVE_THRD_CREATE \
  -I"$g/mesa/include" -I"$g/mesa/src" -I"$g/mesa/src/vulkan/runtime" \
  -I"$g/mesa/src/vulkan/util" -I"$b/mesa/src/vulkan/util" -I"$b/mesa/src/vulkan/runtime" \
  "$here/android_native_driver.c" "$here/test_android_native_driver.c" -o "$out/android-native-driver-test"
"$out/android-native-driver-test"
cc -std=gnu11 -Wall -Wextra -Werror -DVK_USE_PLATFORM_ANDROID_KHR \
  -DHAVE_ENDIAN_H -DHAVE_STRUCT_TIMESPEC -DHAVE_PTHREAD -DHAVE_THRD_CREATE \
  -I"$g/mesa/include" -I"$g/mesa/src" -I"$g/mesa/src/vulkan/runtime" \
  -I"$g/mesa/src/vulkan/util" -I"$b/mesa/src/vulkan/util" -I"$b/mesa/src/vulkan/runtime" \
  "$here/android_native_image.c" "$here/test_android_native_image.c" -o "$out/android-native-image-test"
"$out/android-native-image-test"
cc -std=c11 -Wall -Wextra -Werror -I"$g/mesa/include" \
  "$here/test_android_swapchain_flow.c" "$here/android_swapchain_flow.c" \
  "$here/android_swapchain_slots.c" "$here/android_buffer_queue.c" -o "$out/android-swapchain-flow-test"
"$out/android-swapchain-flow-test"
cc -std=c11 -Wall -Wextra -Werror -I"$g/mesa/include" \
  "$here/test_android_swapchain_object.c" "$here/android_swapchain_object.c" \
  "$here/android_swapchain_flow.c" "$here/android_swapchain_slots.c" "$here/android_buffer_queue.c" \
  -o "$out/android-swapchain-object-test"
"$out/android-swapchain-object-test"
echo 'PASS: surface, queue, slot, dispatch and acquire/present ownership flow (CPU only)'
