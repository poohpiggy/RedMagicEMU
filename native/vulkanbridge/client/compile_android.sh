#!/bin/sh
# Arguments: matching gfxstream source, guest build, AOSP headers, NDK, output directory, optional newer registry.
set -eu
[ "$#" -eq 5 ] || [ "$#" -eq 6 ] || { echo "usage: $0 SOURCE BUILD AOSP NDK OUT [NEWER_VK_XML]" >&2; exit 2; }
src=$1
build=$2
a=$3
ndk=$4
out=$5
g="$src/guest"
b="$build/guest"
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$out"
if [ "$#" -eq 6 ]; then
  python3 "$here/generate_public_api.py" "$g/mesa/src/vulkan/registry/vk.xml" \
    "$b/mesa/src/vulkan/runtime/vk_dispatch_trampolines.c" "$out/public.c" "$6"
else
  python3 "$here/generate_public_api.py" "$g/mesa/src/vulkan/registry/vk.xml" \
    "$b/mesa/src/vulkan/runtime/vk_dispatch_trampolines.c" "$out/public.c"
fi
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=gnu11 -fPIC -fvisibility=hidden -DANDROID -DANDROID_API_LEVEL=35 \
  ${RME_VULKAN_TRACE_PROCS:+-DRME_TRACE_VULKAN_PROCS} \
  -DVK_USE_PLATFORM_ANDROID_KHR -DVK_GFXSTREAM_STRUCTURE_TYPE_EXT \
  -DHAVE_STRUCT_TIMESPEC -DHAVE_PTHREAD -DHAVE_THRD_CREATE -DHAVE_LINUX_FUTEX_H \
  -I"$here" -I"$g/vulkan" -I"$g/vulkan_enc" -I"$g/mesa/include" -I"$g/mesa/src" \
  -I"$g/mesa/src/util" -I"$g/mesa/src/vulkan/runtime" -I"$g/mesa/src/vulkan/util" \
  -I"$b/vulkan_enc" -I"$b/mesa/src/vulkan/util" -I"$b/mesa/src/vulkan/runtime" \
  -I"$src/common/vulkan/include" -I"$a/aosp-liblog-includes" \
  -I"$a/aosp-libcutils-includes" -I"$a/aosp-libbase-files/include" \
  -I"$a/src/subprojects/libdrm-2.4.133/include/drm" \
  -c "$out/public.c" -o "$out/public.o"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=gnu11 -fPIC -DANDROID -DANDROID_API_LEVEL=35 -DVK_USE_PLATFORM_ANDROID_KHR \
  -DVK_GFXSTREAM_STRUCTURE_TYPE_EXT -DHAVE_STRUCT_TIMESPEC -DHAVE_PTHREAD -DHAVE_THRD_CREATE -DHAVE_LINUX_FUTEX_H \
  -I"$g/mesa/include" -I"$g/mesa/src" -I"$g/mesa/src/util" \
  -I"$g/mesa/src/vulkan/runtime" -I"$g/mesa/src/vulkan/util" \
  -I"$b/vulkan_enc" -I"$b/mesa/src/vulkan/util" -I"$b/mesa/src/vulkan/runtime" \
  -I"$src/common/vulkan/include" -I"$a/aosp-liblog-includes" \
  -I"$a/aosp-libcutils-includes" -I"$a/aosp-libbase-files/include" \
  -I"$a/src/subprojects/libdrm-2.4.133/include/drm" \
  -c "$b/vulkan_enc/gfxstream_vk_entrypoints.c" -o "$out/gfxstream_vk_entrypoints.o"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -I"$g/mesa/include" \
  -c "$here/android_surface.c" -o "$out/android_surface.o"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -I"$g/mesa/include" \
  -I"$a/aosp-nativewindow" -I"$a/aosp-nativebase" -I"$a/aosp-libcutils-includes" \
  -I"$a/aosp-core/libsystem/include" -MD -MF "$out/android_surface_android.d" \
  -c "$here/android_surface_android.c" -o "$out/android_surface_android.o"
if grep -q 'android_stub/' "$out/android_surface_android.d"; then
  echo 'unexpected Mesa Android stub header' >&2; exit 1
fi
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -I"$g/mesa/include" \
  -c "$here/android_buffer_queue.c" -o "$out/android_buffer_queue.o"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -I"$g/mesa/include" \
  -I"$a/aosp-nativewindow" -I"$a/aosp-nativebase" -I"$a/aosp-libcutils-includes" \
  -I"$a/aosp-core/libsystem/include" -MD -MF "$out/android_buffer_queue_android.d" \
  -c "$here/android_buffer_queue_android.c" -o "$out/android_buffer_queue_android.o"
if grep -q 'android_stub/' "$out/android_buffer_queue_android.d"; then
  echo 'unexpected Mesa Android stub header' >&2; exit 1
fi
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -I"$g/mesa/include" \
  -c "$here/android_swapchain_slots.c" -o "$out/android_swapchain_slots.o"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=gnu11 -fPIC -fvisibility=hidden -DANDROID -DANDROID_API_LEVEL=35 \
  -DVK_USE_PLATFORM_ANDROID_KHR -DVK_GFXSTREAM_STRUCTURE_TYPE_EXT \
  -DHAVE_STRUCT_TIMESPEC -DHAVE_PTHREAD -DHAVE_THRD_CREATE -DHAVE_LINUX_FUTEX_H \
  -I"$g/mesa/include" -I"$g/mesa/src" -I"$g/mesa/src/util" \
  -I"$g/mesa/src/vulkan/runtime" -I"$g/mesa/src/vulkan/util" \
  -I"$b/mesa/src/vulkan/util" -I"$b/mesa/src/vulkan/runtime" \
  -I"$a/aosp-libcutils-includes" -I"$a/aosp-liblog-includes" \
  -MD -MF "$out/android_native_driver.d" \
  -c "$here/android_native_driver.c" -o "$out/android_native_driver.o"
if grep -q 'android_stub/' "$out/android_native_driver.d"; then
  echo 'unexpected Mesa Android stub header' >&2; exit 1
fi
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=gnu11 -fPIC -fvisibility=hidden -DANDROID -DANDROID_API_LEVEL=35 \
  -DVK_USE_PLATFORM_ANDROID_KHR -DVK_GFXSTREAM_STRUCTURE_TYPE_EXT \
  -DHAVE_STRUCT_TIMESPEC -DHAVE_PTHREAD -DHAVE_THRD_CREATE -DHAVE_LINUX_FUTEX_H \
  -I"$g/mesa/include" -I"$g/mesa/src" -I"$g/mesa/src/util" \
  -I"$g/mesa/src/vulkan/runtime" -I"$g/mesa/src/vulkan/util" \
  -I"$b/mesa/src/vulkan/util" -I"$b/mesa/src/vulkan/runtime" \
  -I"$a/aosp-libcutils-includes" -I"$a/aosp-liblog-includes" \
  -MD -MF "$out/android_native_image.d" \
  -c "$here/android_native_image.c" -o "$out/android_native_image.o"
if grep -q 'android_stub/' "$out/android_native_image.d"; then
  echo 'unexpected Mesa Android stub header' >&2; exit 1
fi
for name in android_swapchain_flow android_swapchain_flow_android android_swapchain_object; do
  "$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
    -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -I"$g/mesa/include" \
    -c "$here/$name.c" -o "$out/$name.o"
done
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf" -h "$out/public.o" | grep -E 'Class:|Machine:|Type:'
