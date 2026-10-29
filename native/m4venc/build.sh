#!/bin/sh
# libaemum4v.so (arm64): AOSP's PacketVideo MPEG-4/H.263 encoder for VideoEncoderFeed.kt
NDK=${NDK:-$ANDROID_NDK_HOME}
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
[ -x "$BIN/clang++" ] || BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
cd "$(dirname "$0")"
OUT=${1:-../../app/src/main/jniLibs/arm64-v8a/libaemum4v.so}
"$BIN/clang++" --target=aarch64-linux-android29 -O2 -fPIC -shared -Wl,-soname,libaemum4v.so -w \
    -DOSCL_IMPORT_REF= -DOSCL_UNUSED_ARG='(void)' -DOSCL_EXPORT_REF= -Iinclude -Isrc \
    -static-libstdc++ -o "$OUT" m4venc_jni.cpp src/*.cpp
"$BIN/llvm-strip" "$OUT" 2>/dev/null || true
