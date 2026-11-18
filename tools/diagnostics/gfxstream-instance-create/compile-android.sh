#!/bin/sh
# Compile the patched real source in a temporary tree using existing inputs.
# Arguments: gfxstream source, generated guest build, AOSP includes, Android NDK.
set -eu
if [ "$#" -ne 4 ]; then
    echo "usage: $0 SOURCE BUILD_GUEST AOSP_HEADERS ANDROID_NDK" >&2
    exit 2
fi
src=$1
build=$2
aosp=$3
ndk=$4
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
g="$src/guest"
b="$build/guest"
compiler="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++"
for path in "$compiler" "$g/vulkan/gfxstream_vk_device.cpp" "$b/vulkan_enc/gfxstream_vk_entrypoints.h" \
    "$aosp/aosp-liblog-includes" "$aosp/aosp-libcutils-includes" "$aosp/aosp-libbase-files/include" \
    "$aosp/src/subprojects/libdrm-2.4.133/include/drm"; do
    [ -e "$path" ] || { echo "missing input: $path" >&2; exit 2; }
done
tmp=$(mktemp -d /var/tmp/rme-vk-instance-android.XXXXXXXX)
echo "ARTIFACTS=$tmp"
mkdir -p "$tmp/guest/vulkan"
cp "$g/vulkan/gfxstream_vk_device.cpp" "$tmp/guest/vulkan/"
ln -s "$g/vulkan_enc" "$tmp/guest/vulkan_enc"
patch --batch --fuzz=0 -p1 -d "$tmp" -i "$here/fix.patch"
"$compiler" -std=gnu++20 -fPIC "-ffile-prefix-map=$tmp=/rme/gfxstream-build" \
    -DANDROID -DANDROID_API_LEVEL=35 -DVK_USE_PLATFORM_ANDROID_KHR \
    -DVK_GFXSTREAM_STRUCTURE_TYPE_EXT -DHAVE_STRUCT_TIMESPEC -DHAVE_PTHREAD -DHAVE_THRD_CREATE \
    -I"$g/vulkan" -I"$g/vulkan_enc" -I"$g/mesa/include" -I"$g/mesa/src" -I"$g/mesa/src/util" \
    -I"$g/mesa/src/vulkan/runtime" -I"$g/mesa/src/vulkan/util" -I"$g/OpenglSystemCommon" \
    -I"$g/OpenglCodecCommon" -I"$g/OpenglCodecCommon/include" -I"$g/iostream/include" \
    -I"$g/qemupipe/include" -I"$g/qemupipe/include-types" -I"$g/android-emu" \
    -I"$g/android/include" -I"$g/include" -I"$g/renderControl_enc" -I"$g/GoldfishAddressSpace/include" \
    -I"$g/platform/include" -I"$g/gralloc_cb/include" -I"$g/GLESv1_enc" -I"$g/GLESv2_enc" \
    -I"$b/vulkan_enc" -I"$b/mesa/src/vulkan/util" -I"$b/mesa/src/vulkan/runtime" \
    -I"$src/common/vulkan/include" -I"$src/common/opengl/include" \
    -I"$aosp/aosp-liblog-includes" -I"$aosp/aosp-libcutils-includes" \
    -I"$aosp/aosp-libbase-files/include" -I"$aosp/src/subprojects/libdrm-2.4.133/include/drm" \
    -MD -MF "$tmp/headers.d" -c "$tmp/guest/vulkan/gfxstream_vk_device.cpp" -o "$tmp/gfxstream_vk_device.o"
if grep -q 'android_stub/' "$tmp/headers.d"; then
    echo 'unexpected Mesa Android stub header' >&2
    exit 1
fi
readelf="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
"$readelf" -h "$tmp/gfxstream_vk_device.o" | grep -E 'Class:|Machine:|Type:'
sha256sum "$tmp/gfxstream_vk_device.o"
echo 'ANDROID_ARM64_OBJECT_OK (no Android stub headers; not linked or run)'
