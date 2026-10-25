#!/bin/sh
# One 32-bit kernel (aemu32) per Android API level, next to the arm64 one of native/guest64/<tag>:
# sh build32.sh <tag>. It runs the firmware's 32-bit ARM programs natively on a phone whose cores execute AArch32;
# the arm64 kernel starts it instead of qemu-arm there. Static, no libc, linked at a fixed address the seccomp
# filter recognises (IMAGE_BASE in run32.c). Packed with the arm64 libraries: an app process may exec it.
TAG=$1
[ -d "$(dirname "$0")/$TAG" ] && [ -n "$TAG" ] || { echo "usage: sh build32.sh <tag>   (one of: $(cd "$(dirname "$0")" && ls -d [0-9]* | tr '\n' ' '))"; exit 1; }
cd "$(dirname "$0")/$TAG" || exit 1
NDK=${NDK:-$ANDROID_NDK_HOME}
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
[ -x "$BIN/clang" ] || BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
DIR=${2:-../../../app/src/main/jniLibs/arm64-v8a}
OUT="$DIR/libaemu32_$TAG.so"
"$BIN/clang" --target=armv7a-linux-androideabi21 -marm -O2 -static -nostdlib -ffreestanding -fno-builtin -fno-stack-protector \
  -fno-pic -no-pie -Wall -Wno-unused-function -Wl,--image-base=0x7e000000 -Wl,--build-id=none -Wl,-z,norelro \
  -o "$OUT" start32.S run32.c || exit 1
"$BIN/llvm-strip" "$OUT" 2>/dev/null || true
