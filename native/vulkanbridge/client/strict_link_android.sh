#!/bin/sh
# Offline link diagnostic only. No device operations or deployment.
set -eu
[ "$#" -eq 8 ] || { echo "usage: $0 NDK MESA_BUILD ENCODER_OBJECTS OUTPUT STREAM_OBJECTS GUEST_LIBS GLES_LIBS LIBDRM" >&2; exit 2; }
ndk=$1
mesa=$2
encoder=$3
out=$4
stream=$5
guest=$6
gles=$7
drm=$8
m="$mesa/guest/mesa/src"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++" \
  -shared -Wl,--no-undefined -Wl,--error-limit=0 -Wl,-soname,libvulkan.so -o "$out/libvulkan-client.so" \
  "$out/public.o" "$out/gfxstream_vk_entrypoints.o" "$out/device.o" "$out/command.o" \
  "$out/android_surface.o" "$out/android_surface_android.o" \
  "$out/android_buffer_queue.o" "$out/android_buffer_queue_android.o" \
  "$out/android_swapchain_slots.o" "$out/android_native_driver.o" \
  "$out/android_native_image.o" \
  "$out/android_swapchain_flow.o" "$out/android_swapchain_flow_android.o" \
  "$out/android_swapchain_object.o" \
  "$encoder"/*.o "$out/emu_pipe.o" "$stream"/*.o \
  -Wl,--start-group "$m/vulkan/runtime/libvulkan_runtime.a" \
  "$m/vulkan/util/libvulkan_util.a" "$m/vulkan/wsi/libvulkan_wsi.a" \
  "$m/util/libmesa_util.a" "$m/util/libxmlconfig.a" "$m/c11/impl/libmesa_util_c11.a" \
  -Wl,--end-group "$gles/libRme_gles1_enc.so" "$gles/libRme_gles2_enc.so" \
  "$guest/libcutils.so" "$guest/libbase.so" "$guest/libsync.so" "$guest/libnativewindow.so" \
  "$drm" -llog -landroid
