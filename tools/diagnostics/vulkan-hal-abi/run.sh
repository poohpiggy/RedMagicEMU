#!/bin/sh
# Exercise the existing upstream Android HAL entrypoint, without a backend.
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
source=${GFXSTREAM_HAL_SOURCE:-/var/tmp/rme-gfxstream-build.hTnuwsWV/src/guest/vulkan/gfxstream_vk_android.cpp}
headers=${AOSP_STUB_INCLUDE:-/var/tmp/rme-kumquat-research.r6Wg21lO/mesa-6e4a/include/android_stub}
ndk=${ANDROID_NDK_LINUX:-/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c}
bin="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
sysinclude="$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include"
for path in "$source" "$headers/hardware/hwvulkan.h" "$bin/aarch64-linux-android35-clang++" "$sysinclude/vulkan/vulkan.h"; do
    if [ ! -f "$path" ]; then echo "missing dependency: $path" >&2; exit 2; fi
done
tmp=$(mktemp -d /var/tmp/rme-vulkan-hal-abi.XXXXXXXX) || exit 2
echo "artifacts=$tmp"
# Compile the original common-subset source as C for the host test; this avoids
# inventing an implementation and uses only the three declared test backends.
"${CC:-cc}" -x c -std=gnu11 -Dstatic_assert=_Static_assert \
    -I "$here/stubs" -I "$headers" -idirafter "$sysinclude" \
    "$source" "$here/abi_test.c" -o "$tmp/abi_test" || exit 2
# Separately compile that exact upstream source as C++ for Android ARM64.
"$bin/aarch64-linux-android35-clang++" -std=gnu++20 -fPIC \
    -I "$here/stubs" -I "$headers" -c "$source" -o "$tmp/hal-arm64.o" || exit 2
"$bin/llvm-readelf" -h "$tmp/hal-arm64.o" | grep -q 'Machine:.*AArch64' || exit 2
"$bin/llvm-readelf" -s "$tmp/hal-arm64.o" | grep -q ' HMI$' || exit 2
echo 'ARM64_OBJECT_OK (compile and global symbol only)'
ulimit -c 0
failed=0
for name in open invalid close; do
    echo "CASE $name"
    "$tmp/abi_test" "$name"
    status=$?
    echo "case_exit=$status"
    [ "$status" -eq 0 ] || failed=1
done
if [ "$failed" -ne 0 ]; then echo 'UPSTREAM_HAL_ABI_CHECK_FAILED' >&2; exit 1; fi
echo 'UPSTREAM_HAL_ABI_CHECK_OK'
