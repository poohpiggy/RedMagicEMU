#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
    echo "usage: $0 GFXSTREAM_SOURCE ANDROID_NDK" >&2
    exit 2
fi
src=$1
ndk=$2
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
compiler="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++"
readelf="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
for path in "$compiler" "$readelf" "$src/guest/qemupipe/qemu_pipe_common.cpp" \
    "$src/guest/qemupipe/include/qemu_pipe_bp.h" "$src/guest/qemupipe/include-types/qemu_pipe_types_bp.h"; do
    [ -f "$path" ] || { echo "missing: $path" >&2; exit 2; }
done
actual=$(sha256sum "$src/guest/qemupipe/qemu_pipe_common.cpp" | cut -d' ' -f1)
[ "$actual" = '70835e3f306d15a9fe7520999449fc0862766e47acb4a7eab3bb369ba3e63a79' ] || {
    echo 'unexpected QEMU pipe source' >&2; exit 2;
}
actual=$(sha256sum "$src/guest/qemupipe/include/qemu_pipe_bp.h" | cut -d' ' -f1)
[ "$actual" = '1dcad14b719e54f1eb6bece6a11cd87ad6f4c2fb69b70ba072e68091d4c130b8' ] || {
    echo 'unexpected QEMU pipe header' >&2; exit 2;
}
actual=$(sha256sum "$src/guest/qemupipe/include-types/qemu_pipe_types_bp.h" | cut -d' ' -f1)
[ "$actual" = 'd15abb665076ba7f4e9dac867b16ab4bbcd53a061dee18c1471136fd85b4bd25' ] || {
    echo 'unexpected QEMU pipe types header' >&2; exit 2;
}
tmp=$(mktemp -d /var/tmp/rme-qemupipe-android.XXXXXXXX)
echo "ARTIFACTS=$tmp"
mkdir -p "$tmp/guest/qemupipe"
cp "$src/guest/qemupipe/qemu_pipe_common.cpp" "$tmp/guest/qemupipe/"
patch --batch --fuzz=0 -p1 -d "$tmp" -i "$here/fix.patch"
"$compiler" -std=gnu++17 -Wall -Wextra -Werror -fPIC -O2 \
    "-ffile-prefix-map=$tmp=/rme/qemupipe-build" \
    -I"$src/guest/qemupipe/include" -I"$src/guest/qemupipe/include-types" \
    -c "$tmp/guest/qemupipe/qemu_pipe_common.cpp" -o "$tmp/qemu_pipe_common.o"
"$readelf" -h "$tmp/qemu_pipe_common.o" > "$tmp/elf.txt"
grep -q 'Machine:.*AArch64' "$tmp/elf.txt"
grep -q 'Type:.*REL' "$tmp/elf.txt"
sha256sum "$tmp/qemu_pipe_common.o"
echo 'ANDROID_ARM64_OBJECT_OK (not linked or run; no Guest or device)'
