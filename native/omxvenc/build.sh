#!/bin/sh
# libOmxVidEnc.so for Qualcomm 1.x-2.x (omx_venc.c): ARMv7 Thumb, the firmware's libc/liblog, classic hash table
NDK=${NDK:-$ANDROID_NDK_HOME}
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
[ -x "$BIN/clang" ] || BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
OUT=${1:-../../app/src/main/assets/engines/common/libOmxVidEnc.aemu.so}
T=$(mktemp -d)
echo 'int __android_log_print(void){return 0;}' > "$T/l.c"
"$BIN/clang" --target=armv7a-linux-androideabi21 -nostdlib -shared -fPIC -Wl,-soname,liblog.so -o "$T/liblog.so" "$T/l.c"
"$BIN/clang" --target=armv7a-linux-androideabi21 -march=armv7-a -mthumb -mfloat-abi=softfp -Os -fPIC -shared \
  -nostdlib -ffreestanding -fno-builtin -fvisibility=hidden -fno-stack-protector -Wall \
  -Wl,--hash-style=sysv -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined -Wl,-soname,libOmxVidEnc.so \
  -o "$OUT" omx_venc.c -L"$T" -llog -lc
"$BIN/llvm-strip" "$OUT" 2>/dev/null || true
rm -rf "$T"
