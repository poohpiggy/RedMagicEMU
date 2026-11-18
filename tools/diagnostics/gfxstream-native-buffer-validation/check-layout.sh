#!/bin/sh
# Compile-time ARM64 layout check only; no runtime or device access.
set -eu
[ "$#" -eq 5 ] || { echo "usage: $0 NDK SOURCE AOSP_HEADERS AOSP16_NATIVE_BUFFER_HEADER OUTPUT" >&2; exit 2; }
ndk=$1
src=$2
aosp=$3
aosp_header=$4
out=$5
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$out"
cc="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
"$cc" -std=c11 -DANDROID -DANDROID_API_LEVEL=35 -DVK_USE_PLATFORM_ANDROID_KHR \
  -I"$src/guest/mesa/include" -I"$aosp/aosp-libcutils-includes" \
  -include "$src/guest/mesa/include/vulkan/vk_android_native_buffer.h" \
  -c "$here/layout.c" -o "$out/d7a-layout.o"
"$cc" -std=c11 -DANDROID -DANDROID_API_LEVEL=35 -DVK_USE_PLATFORM_ANDROID_KHR -DRME_AOSP_SPEC11 \
  -I"$src/guest/mesa/include" -I"$aosp/aosp-libcutils-includes" -include "$aosp_header" \
  -c "$here/layout.c" -o "$out/aosp16-layout.o"
echo 'PASS: Android ARM64 d7a spec 9 and Android 16 spec 11 common prefix offsets'
