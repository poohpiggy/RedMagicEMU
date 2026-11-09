#!/bin/sh
# Сборка хост-помощника libaemuhost.so (arm64, API 29 — там появился fdsan)
NDK=${NDK:-$ANDROID_NDK_HOME}
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
[ -x "$BIN/clang" ] || BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
OUT=${1:-../../app/src/main/jniLibs/arm64-v8a/libaemuhost.so}
"$BIN/clang" --target=aarch64-linux-android29 -Os -fPIC -shared -Wl,-soname,libaemuhost.so -o "$OUT" hostjni.c radio_host_gate.c radio_host_gate_jni.c vibration_host_gate.c vibration_host_gate_jni.c ../gueststubs/radio-bridge/gate.c ../gueststubs/radio-bridge/receive.c
"$BIN/llvm-strip" "$OUT" 2>/dev/null || true
"$BIN/clang" --target=aarch64-linux-android29 -Os -fPIE -pie -o ../../app/src/main/jniLibs/arm64-v8a/libaemu-launch.so guest_launch.c vibration_metadata_guard.c || exit 1
"$BIN/llvm-strip" ../../app/src/main/jniLibs/arm64-v8a/libaemu-launch.so 2>/dev/null || true
