#!/bin/sh
# build.sh: the guest sampler for the phone (run as: adb shell run-as app.aemu files/gprof <pid|comm> [secs] [thread] [delay])
cd "$(dirname "$0")" && /e/aemu_work/ndk/android-ndk-r28c/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe --target=aarch64-linux-android29 -O2 gprof.c -o gprof
