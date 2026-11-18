#!/bin/sh
# Build the real d7a device source with independently tested fixes.
set -eu
[ "$#" -eq 6 ] || { [ "$#" -eq 7 ] && [ "$7" = "--emu-vulkan-pipe" ]; } || {
  echo "usage: $0 SOURCE GUEST_BUILD AOSP NDK OUTPUT PATCHED_ENCODER_SOURCE [--emu-vulkan-pipe]" >&2
  exit 2
}
src=$1
build=$2
a=$3
ndk=$4
out=$5
if ! grep -q 'VkResult unwrap_vkAcquireImageANDROID_nativeFenceFd' "$6/ResourceTracker.h"; then
  echo 'patched encoder header required; run rebuild_encoder_candidates.py first' >&2
  exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
g="$src/guest"
b="$build/guest"
tmp=$(mktemp -d /var/tmp/rme-vk-client-device.XXXXXXXX)
mkdir -p "$tmp/guest/vulkan" "$out"
cp "$g/vulkan/gfxstream_vk_device.cpp" "$tmp/guest/vulkan/"
ln -s "$6" "$tmp/guest/vulkan_enc"
patch --batch --fuzz=0 -p1 -d "$tmp" -i "$root/tools/diagnostics/gfxstream-d7a-null-instance/fix.patch"
patch --batch --fuzz=0 -p1 -d "$tmp" -i "$root/tools/diagnostics/gfxstream-instance-create/fix.patch"
patch --batch --fuzz=0 -p1 -d "$tmp" -i "$root/tools/diagnostics/gfxstream-null-host-connection/fix.patch"
patch --batch --fuzz=0 -p1 -d "$tmp" -i "$root/tools/diagnostics/gfxstream-wsi-advertisement/fix.patch"
patch --batch --fuzz=0 -p1 -d "$tmp" -i "$root/tools/diagnostics/gfxstream-native-buffer-version/fix.patch"
if [ "$#" -eq 7 ]; then
  patch --batch --fuzz=0 -p1 -d "$tmp" -i "$root/native/vulkanbridge/client/device_emu_pipe.patch"
fi
commands="$g/vulkan/gfxstream_vk_cmd.cpp"
if [ "$#" -eq 7 ]; then
  cp "$commands" "$tmp/guest/vulkan/"
  patch --batch --fuzz=0 -p1 -d "$tmp" -i "$root/native/vulkanbridge/client/command_pool_emu_pipe.patch"
  commands="$tmp/guest/vulkan/gfxstream_vk_cmd.cpp"
fi
compile_vulkan_device_source() {
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++" \
  -std=gnu++20 -fPIC -DANDROID -DANDROID_API_LEVEL=35 -DGFXSTREAM_ENABLE_GUEST_GOLDFISH=1 \
  -DVK_ANDROID_native_buffer -DVK_EXT_device_memory_report -DVK_GOOGLE_gfxstream \
  -DVK_USE_PLATFORM_ANDROID_KHR -DVK_GFXSTREAM_STRUCTURE_TYPE_EXT \
  -DHAVE_STRUCT_TIMESPEC -DHAVE_PTHREAD -DHAVE_THRD_CREATE -DHAVE_LINUX_FUTEX_H \
  -I"$g/vulkan" -I"$tmp/guest/vulkan_enc" -I"$g/vulkan_enc" -I"$g/mesa/include" -I"$g/mesa/src" -I"$g/mesa/src/util" \
  -I"$g/mesa/src/vulkan/runtime" -I"$g/mesa/src/vulkan/util" -I"$g/OpenglSystemCommon" \
  -I"$g/OpenglCodecCommon" -I"$g/OpenglCodecCommon/include" -I"$g/iostream/include" \
  -I"$g/qemupipe/include" -I"$g/qemupipe/include-types" -I"$g/android-emu" \
  -I"$g/android/include" -I"$g/include" -I"$g/renderControl_enc" -I"$g/GoldfishAddressSpace/include" \
  -I"$g/platform/include" -I"$g/gralloc_cb/include" -I"$g/GLESv1_enc" -I"$g/GLESv2_enc" \
  -I"$b/vulkan_enc" -I"$b/mesa/src/vulkan/util" -I"$b/mesa/src/vulkan/runtime" \
  -I"$src/common/vulkan/include" -I"$src/common/opengl/include" \
  -I"$a/aosp-liblog-includes" -I"$a/aosp-libcutils-includes" -I"$a/aosp-libbase-files/include" \
  -I"$a/src/subprojects/libdrm-2.4.133/include/drm" \
  -MD -MF "$out/$2.d" -c "$1" -o "$out/$2.o"
if grep -q 'android_stub/' "$out/$2.d"; then
  echo 'unexpected Mesa Android stub header' >&2; exit 1
fi
echo "built $out/$2.o"
}
compile_vulkan_device_source "$tmp/guest/vulkan/gfxstream_vk_device.cpp" device
compile_vulkan_device_source "$commands" command
