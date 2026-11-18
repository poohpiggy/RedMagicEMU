#!/bin/sh
set -eu
if [ "$#" -ne 3 ]; then
    echo "usage: $0 <gfxstream-root> <android-ndk> <output-dir>" >&2
    exit 2
fi
root=$1
ndk=$2
out=$3
dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
repo=$(CDPATH= cd -- "$dir/../.." && pwd)
test -d "$out"
fake="$repo/tools/diagnostics/gfxstream-android-surface/test_include"
gcc -std=c11 -Wall -Wextra -Werror -DVK_USE_PLATFORM_ANDROID_KHR \
    -I"$fake" -I"$root/src/common/vulkan/include" \
    "$dir/surface_jni.c" "$dir/host_surface.c" "$dir/tests/surface_jni_test.c" \
    -pthread -o "$out/surface_jni_test"
"$out/surface_jni_test"
cc="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
"$cc" -std=c11 -Wall -Wextra -Werror -DVK_USE_PLATFORM_ANDROID_KHR \
    -c "$dir/surface_jni.c" -o "$out/surface_jni-android.o"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf" -h "$out/surface_jni-android.o" | grep -E 'Class:|Machine:'
python3 "$dir/tests/link_surface_jni.py" "$ndk" "$out"
