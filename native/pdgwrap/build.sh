#!/bin/sh
# NDK=E:/aemu_work/ndk/android-ndk-r28c sh native/pdgwrap/build.sh
cd "$(dirname "$0")"
CC=$(ls -d "$NDK"/toolchains/llvm/prebuilt/*/bin)/aarch64-linux-android26-clang
[ -x "$CC" ] || CC="$CC.cmd"
"$CC" -O2 -fPIE -pie -s -o ../../app/src/main/jniLibs/arm64-v8a/libpdgwrap.so wrap.c
