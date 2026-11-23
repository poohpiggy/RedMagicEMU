#!/bin/sh
# build.sh: the guest sampler for the phone (run as: adb shell run-as app.aemu files/gprof <pid|comm> [secs] [thread] [delay])
set -e
cd "$(dirname "$0")"
NDK=${NDK:-${ANDROID_NDK_HOME:?Set NDK or ANDROID_NDK_HOME to the Android NDK directory}}
clang="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
"$clang" --target=aarch64-linux-android29 -O2 gprof.c -o gprof
