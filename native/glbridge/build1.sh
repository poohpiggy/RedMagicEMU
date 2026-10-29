#!/bin/sh
# The GLES 1 bridge of Android 1.x (gen1.py, guest1.c, fwd1.h): libhEGL.so, libhGLESv1_CM.so and libhgl.so into the assets
# (TreeFixer installs them); the host side is in libaemugl.so (build.sh, host1.c).
NDK=${NDK:-$ANDROID_NDK_HOME}
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
[ -x "$BIN/clang" ] || BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
cd "$(dirname "$0")" || exit 1
python gen1.py "$BIN/../sysroot/usr/include" || exit 1
A=../../app/src/main/assets/engines/common
# 1.x bionic: no TLS, an old linker (classic hash table), no __aeabi_* division in libc: the compiler's helpers go in
RT=$("$BIN/clang" --target=armv7a-linux-androideabi21 --print-libgcc-file-name)
F="--target=armv7a-linux-androideabi21 -march=armv7-a -mthumb -O2 -fPIC -shared -nostdlib -fvisibility=hidden
   -fno-stack-protector -Wall -Wno-unused-function -Wl,--hash-style=sysv -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined"
"$BIN/clang" $F -Wl,-soname,libhEGL.so -o $A/libhEGL.aemu.so guest1.c "$RT" -lc -ldl -llog || exit 1
"$BIN/clang" $F -Wl,-soname,libhGLESv1_CM.so -o $A/libhGLESv1_CM.aemu.so g1_fwd.c "$RT" -lc -ldl || exit 1
# AOSP Cupcake/Donut libEGL loads one library, libhgl.so, for both EGL and GL (egl.cpp load_driver("libhgl.so"))
"$BIN/clang" $F -DSELF_SO='"libhgl.so"' -Wl,-soname,libhgl.so -o $A/libhgl.aemu.so guest1.c "$RT" -lc -ldl -llog || exit 1
"$BIN/llvm-strip" $A/libhEGL.aemu.so $A/libhGLESv1_CM.aemu.so $A/libhgl.aemu.so
ls -la $A/libhEGL.aemu.so $A/libhGLESv1_CM.aemu.so $A/libhgl.aemu.so
