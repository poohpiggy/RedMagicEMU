#!/bin/sh
# Configure and build only real Mesa components for offline link diagnostics.
# pkg-config metadata must resolve the real libdrm and Android system library
# names; this script does not create or link replacement system libraries.
set -eu
[ "$#" -eq 5 ] || { echo "usage: $0 SOURCE BUILD CROSS_FILE AOSP_HEADERS HARDWARE_HEADERS" >&2; exit 2; }
src=$1
build=$2
cross=$3
a=$4
hardware=$5
flags="-DANDROID -DVK_USE_PLATFORM_ANDROID_KHR -DVK_GFXSTREAM_STRUCTURE_TYPE_EXT -I$a/aosp-libcutils-includes -I$a/aosp-liblog-includes -I$a/aosp-libbase-files/include -I$hardware -I$a/aosp-nativewindow -I$a/aosp-core/libsystem/include"
meson setup "$build" "$src" --cross-file "$cross" -Dgfxstream-build=guest \
  -Dbuild-tests=false -Dplatforms=android -Dplatform-sdk-version=35 -Dandroid-libbacktrace=disabled \
  -Dvulkan-drivers=gfxstream-experimental -Degl=disabled -Dgles1=disabled -Dgles2=disabled \
  -Dllvm=disabled -Dxmlconfig=disabled -Dzstd=disabled -Dlayout=mirror -Dwrap_mode=nodownload \
  -Dc_std=gnu11 "-Dc_args=$flags" "-Dcpp_args=$flags"
ninja -C "$build" -j4 guest/mesa/src/vulkan/runtime/libvulkan_runtime.a \
  guest/mesa/src/vulkan/util/libvulkan_util.a guest/mesa/src/util/libmesa_util.a \
  guest/mesa/src/vulkan/wsi/libvulkan_wsi.a guest/mesa/src/c11/impl/libmesa_util_c11.a \
  guest/mesa/src/util/libxmlconfig.a
