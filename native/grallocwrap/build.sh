#!/bin/sh
# AEMU_META reserves buffer metadata used by the API 36 graphics bridge and the Android 17 mapper (note.h).
# gralloc of the native arm64 engine (see gralloc_wrap.c): sh build.sh <kernel tag> builds it into that kernel's guest
# files as vendor/lib64/hw/gralloc.default.so; AOSP's own module has to be there as vendor/lib64/libgralloc_aosp.so (outside hw/, where the tree fixer sets aside every gralloc but the default).
TAG=${1:?usage: sh build.sh <kernel tag>}
if [ "$TAG" = 36 ]; then DEFS="${DEFS:-} -DAEMU_META"; fi
NDK=${NDK:-$ANDROID_NDK_HOME}
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
[ -x "$BIN/clang" ] || BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
D="../../app/src/main/assets/engines/n64-$TAG"
[ -f "$D/vendor#lib64#libgralloc_aosp.so" ] || { echo "missing $D/vendor#lib64#libgralloc_aosp.so (AOSP gralloc.default.so of that release)"; exit 1; }
OUT="$D/vendor#lib64#hw#gralloc.default.so"
"$BIN/clang" --target=aarch64-linux-android26 -Os -fPIC -shared \
  -nostdlib -ffreestanding -fno-builtin -fvisibility=hidden -fno-stack-protector -mno-outline-atomics \
  -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined -Wl,-soname,gralloc.default.so \
  $DEFS -o "$OUT" gralloc_wrap.c -ldl -lc || exit 1
"$BIN/llvm-strip" "$OUT" 2>/dev/null || true

# the 32-bit one (ARCH32=1): vendor/lib/hw/gralloc.default.so over vendor/lib/libgralloc_aosp.so, when that is there
if [ -f "$D/vendor#lib#libgralloc_aosp.so" ]; then
  OUT32="$D/vendor#lib#hw#gralloc.default.so"
  "$BIN/clang" --target=armv7a-linux-androideabi26 -Os -fPIC -shared     -nostdlib -ffreestanding -fno-builtin -fvisibility=hidden -fno-stack-protector     -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined -Wl,-soname,gralloc.default.so     $DEFS -o "$OUT32" gralloc_wrap.c -ldl -lc "$(ls "$BIN"/../lib/clang/*/lib/linux/libclang_rt.builtins-arm-android.a | head -1)" || exit 1
  "$BIN/llvm-strip" "$OUT32" 2>/dev/null || true
  ls -la "$OUT32"
fi
