#!/bin/sh
# Compile the unchanged upstream Android.bp ring buffer; do not run it here.
set -eu
[ "$#" -eq 3 ] || { echo "usage: $0 SOURCE ANDROID_NDK OUTPUT" >&2; exit 2; }
src=$1
ndk=$2
out=$3
mkdir -p "$out"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=gnu11 -fPIC -DANDROID -I"$src/guest/android-emu" \
  -c "$src/guest/android-emu/aemu/base/ring_buffer.c" -o "$out/ring_buffer.o"
echo "built $out/ring_buffer.o"
