#!/bin/sh
# CPU-only adapter test with substitute HostConnection/VkEncoder; no device.
set -eu
[ "$#" -eq 3 ] || { echo "usage: $0 GFXSTREAM_SOURCE ANDROID_NDK OUTPUT" >&2; exit 2; }
src=$1
ndk=$2
out=$3
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$out"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang" -std=c11 -Wall -Wextra -Werror \
  -I"$src/guest/mesa/include" -c "$here/android_surface.c" -o "$out/surface.o"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" -nostdlib++ -std=c++17 -Wall -Wextra -Werror -DRME_EXPERIMENTAL_REMOTE_SURFACE \
  -I"$here/test_remote_encoder" -I"$src/guest/mesa/include" \
  "$here/android_remote_surface_encoder.cpp" "$here/test_remote_encoder.cpp" "$out/surface.o" -pthread -o "$out/test"
"$out/test"
echo 'REMOTE_SURFACE_ENCODER_DISPATCH_OK'
