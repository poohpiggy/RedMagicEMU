#!/bin/sh
set -eu
test "$#" -eq 3 || { echo "usage: $0 <gfxstream-root> <ndk> <existing-output-dir>" >&2; exit 2; }
root=$1
ndk=$2
out=$3
test -d "$out"
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../../.." && pwd)
native="$repo/native/vulkanbridge"
fake="$repo/tools/diagnostics/gfxstream-android-surface/test_include"
clang="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
"$clang" -std=c11 -Wall -Wextra -Werror -DVK_USE_PLATFORM_ANDROID_KHR \
    -I"$fake" -I"$root/src/common/vulkan/include" -I"$native" \
    "$here/host_guest_surface.c" "$native/host_surface.c" "$native/surface_jni.c" \
    "$here/host_guest_surface_test.c" -pthread -o "$out/test"
"$out/test"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang" \
    -std=c11 -Wall -Wextra -Werror -fPIC -DVK_USE_PLATFORM_ANDROID_KHR -I"$native" \
    -c "$here/host_guest_surface.c" -o "$out/host_guest_surface_android.o"
echo 'ANDROID_HOST_GUEST_SURFACE_OBJECT_OK'
