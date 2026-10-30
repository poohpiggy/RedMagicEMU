#!/bin/sh
# gralloc.default.so for open32 2.x-7.x (gralloc_abi.c): ARMv7, libc/libdl/liblog only, classic hash table for bionic 2.3
NDK=${NDK:-$ANDROID_NDK_HOME}
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
[ -x "$BIN/clang" ] || BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
cd "$(dirname "$0")" || exit 1
OUT=../../app/src/main/assets/engines/kk/gralloc.abi.so
"$BIN/clang" --target=armv7a-linux-androideabi21 -march=armv7-a -mthumb -Os -fPIC -shared \
  -nostdlib -ffreestanding -fno-builtin -fvisibility=hidden -fno-stack-protector \
  -Wl,--hash-style=sysv -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined -Wl,-soname,gralloc.default.so \
  -o "$OUT" gralloc_abi.c -lc -ldl -llog || exit 1
"$BIN/llvm-strip" "$OUT"
ls -la "$OUT"
