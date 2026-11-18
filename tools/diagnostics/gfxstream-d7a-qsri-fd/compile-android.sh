#!/bin/sh
# Compile the patched real ResourceTracker.cpp; never run the resulting objects.
# Arguments: gfxstream source, generated guest build, AOSP includes, Android NDK,
#            encoder build directory (contains verified platform-header links).
set -eu
if [ "$#" -ne 5 ]; then
    echo "usage: $0 SOURCE BUILD_GUEST AOSP_HEADERS ANDROID_NDK ENCODER_BUILD" >&2
    exit 2
fi
src=$1
build=$2
aosp=$3
ndk=$4
encoder=$5
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
g="$src/guest"
b="$build/guest"
overlay="$encoder/platform-headers"
bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
compiler="$bin/aarch64-linux-android35-clang++"
platform="$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android"
for p in "$compiler" "$g/vulkan_enc/ResourceTracker.cpp" "$b/vulkan_enc/gfxstream_vk_entrypoints.h" \
    "$overlay/android/hardware_buffer.h" "$overlay/android/data_space.h" "$overlay/android/rect.h" \
    "$overlay/vndk/hardware_buffer.h" "$aosp/aosp-core/libsystem/include"; do
    [ -e "$p" ] || { echo "missing input: $p" >&2; exit 2; }
done
for name in hardware_buffer.h data_space.h rect.h; do
    [ "$(readlink -f "$overlay/android/$name")" = "$(readlink -f "$platform/$name")" ] || {
        echo "unexpected NDK header: $name" >&2; exit 2;
    }
done
[ "$(readlink -f "$overlay/vndk/hardware_buffer.h")" = \
  "$(readlink -f "$aosp/aosp-nativewindow-full/include/vndk/hardware_buffer.h")" ] || {
    echo 'unexpected AOSP VNDK header' >&2; exit 2;
}
tmp=$(mktemp -d /var/tmp/rme-vk-qsri-fd-android.XXXXXXXX)
echo "ARTIFACTS=$tmp"
mkdir -p "$tmp/guest/vulkan_enc"
cp "$g/vulkan_enc/ResourceTracker.cpp" "$tmp/guest/vulkan_enc/"
patch --batch --fuzz=0 -p1 -d "$tmp" -i "$here/fix.patch"
for mode in 0 1; do
    "$compiler" -std=gnu++20 -fPIC -DANDROID -DANDROID_API_LEVEL=35 \
        -DVK_USE_PLATFORM_ANDROID_KHR -DVK_GFXSTREAM_STRUCTURE_TYPE_EXT \
        -DHAVE_STRUCT_TIMESPEC -DHAVE_PTHREAD -DHAVE_THRD_CREATE \
        "-DGFXSTREAM_ENABLE_GUEST_GOLDFISH=$mode" '-DLOG_TAG="goldfish_vulkan"' \
        -DVK_ANDROID_native_buffer -DVK_EXT_device_memory_report -DVK_GOOGLE_gfxstream \
        -DVK_NO_PROTOTYPES -fstrict-aliasing -Werror -Wno-missing-field-initializers \
        -Wno-unused-parameter -DHAVE_TIMESPEC_GET "-ffile-prefix-map=$tmp=/rme/qsri-fd-build" \
        -I"$g/vulkan" -I"$g/vulkan_enc" -I"$g/mesa/include" -I"$g/mesa/src" \
        -I"$g/mesa/src/util" -I"$g/mesa/src/vulkan/runtime" -I"$g/mesa/src/vulkan/util" \
        -I"$g/OpenglSystemCommon" -I"$g/OpenglCodecCommon" -I"$g/OpenglCodecCommon/include" \
        -I"$g/iostream/include" -I"$g/qemupipe/include" -I"$g/qemupipe/include-types" \
        -I"$g/android-emu" -I"$g/android/include" -I"$g/include" -I"$g/renderControl_enc" \
        -I"$g/GoldfishAddressSpace/include" -I"$g/platform/include" -I"$g/gralloc_cb/include" \
        -I"$g/GLESv1_enc" -I"$g/GLESv2_enc" -I"$b/vulkan_enc" -I"$b/mesa/src/vulkan/util" \
        -I"$b/mesa/src/vulkan/runtime" -I"$src/common/vulkan/include" -I"$src/common/opengl/include" \
        -I"$aosp/aosp-liblog-includes" -I"$aosp/aosp-libcutils-includes" \
        -I"$aosp/aosp-libbase-files/include" -I"$overlay" -I"$aosp/aosp-core/libsystem/include" \
        -I"$g/mesa/include/android_stub" -I"$aosp/src/subprojects/libdrm-2.4.133/include/drm" \
        -MD -MF "$tmp/ResourceTracker-$mode.d" -c "$tmp/guest/vulkan_enc/ResourceTracker.cpp" \
        -o "$tmp/ResourceTracker-$mode.o"
    "$bin/llvm-readelf" -h "$tmp/ResourceTracker-$mode.o" > "$tmp/ResourceTracker-$mode.elf"
    grep -q 'Type:.*REL' "$tmp/ResourceTracker-$mode.elf"
    grep -q 'Machine:.*AArch64' "$tmp/ResourceTracker-$mode.elf"
    sha256sum "$tmp/ResourceTracker-$mode.o"
done
if grep -q 'egl/goldfish_sync.h' "$tmp/ResourceTracker-0.d"; then
    echo 'Goldfish header unexpectedly used in macro 0 build' >&2; exit 1
fi
grep -q 'egl/goldfish_sync.h' "$tmp/ResourceTracker-1.d"
echo 'ANDROID_ARM64_OBJECTS_OK (not linked or run; no Guest or device access)'
