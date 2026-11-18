#!/bin/sh
set -eu
if [ "$#" -ne 3 ]; then
    echo "usage: $0 <gfxstream-root> <android-ndk> <existing-output-dir>" >&2
    exit 2
fi
root=$1
ndk=$2
out=$3
test -d "$out"
dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
repo=$(CDPATH= cd -- "$dir/../.." && pwd)
fake="$repo/tools/diagnostics/gfxstream-android-surface/test_include"
clang="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
clangxx="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++"
for source in surface_jni host_surface; do
    "$clang" -std=c11 -Wall -Wextra -Werror -DVK_USE_PLATFORM_ANDROID_KHR -I"$fake" -I"$root/src/common/vulkan/include" \
        -c "$dir/$source.c" -o "$out/$source-host.o"
done
"$clang" -std=c11 -Wall -Wextra -Werror -DVK_USE_PLATFORM_ANDROID_KHR -I"$fake" -I"$root/src/common/vulkan/include" \
    -c "$dir/tests/surface_cpp_link_test.c" -o "$out/surface-c-test.o"
"$clangxx" -std=c++17 -nostdlib++ -Wall -Wextra -Werror -DVK_USE_PLATFORM_ANDROID_KHR -I"$fake" -I"$root/src/common/vulkan/include" \
    -c "$dir/tests/surface_cpp_link_probe.cpp" -o "$out/surface-cpp-probe.o"
"$clangxx" -nostdlib++ "$out/surface_jni-host.o" "$out/host_surface-host.o" \
    "$out/surface-c-test.o" "$out/surface-cpp-probe.o" -o "$out/surface-cpp-test"
"$out/surface-cpp-test"

androidcc="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
androidxx="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang++"
for source in surface_jni host_surface; do
    "$androidcc" -std=c11 -Wall -Wextra -Werror -fPIC -DVK_USE_PLATFORM_ANDROID_KHR \
        -c "$dir/$source.c" -o "$out/$source-android-cpp-link.o"
done
"$androidxx" -std=c++17 -Wall -Wextra -Werror -fPIC -DVK_USE_PLATFORM_ANDROID_KHR \
    -c "$dir/tests/surface_cpp_link_probe.cpp" -o "$out/surface-cpp-probe-android.o"
"$androidxx" -nostdlib++ -shared -Wl,--no-undefined "$out/surface_jni-android-cpp-link.o" \
    "$out/host_surface-android-cpp-link.o" "$out/surface-cpp-probe-android.o" -landroid \
    -o "$out/libsurface-cpp-link-test.so"
echo 'ANDROID_CPP_DECODER_TO_C_SURFACE_STRICT_LINK_OK'
