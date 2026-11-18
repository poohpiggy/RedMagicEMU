# Android GLDispatch platform guard

This is a single-file research patch for the fixed AOSP gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`; it is not connected to EMU.
The upstream `GLDispatch.cpp` is Copyright 2011 The Android Open Source Project
and licensed under Apache 2.0; the original file and its copyright notice stay
in the separately supplied source tree.

Android NDK clang defines both `__linux__` and `__ANDROID__`. The original
include guard therefore requests desktop `GL/glx.h` on Android, although this
source file does not use GLX declarations. The patch excludes Android from
that one include and leaves the desktop Linux condition otherwise unchanged.
It does not change function loading, GPU eligibility, or other source files.

## Computer-only verification

The following uses the existing offline source and Meson build configurations;
it downloads nothing and does not load a GLES or Vulkan driver:

```sh
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-android-gldispatch/check.py \
  --source /var/tmp/rme-gfxstream-d7a-android-glesvk.9Zt5VCsh/source \
  --android-build /var/tmp/rme-gfxstream-d7a-android-glesvk.9Zt5VCsh/build \
  --linux-build /var/tmp/rme-gfxstream-d7a-android-glesvk.9Zt5VCsh/linux-build2
```

The script checks the fixed input file SHA-256, reproduces the original Android
missing-header error, applies the patch in a temporary directory, compiles an
AArch64 Android object, and compares original and patched Linux objects after
stripping debug information. Both Linux objects depend on the same temporary
empty `GL/glx.h`; that placeholder is used solely because this particular file
does not use GLX declarations. The test does not validate a real desktop GLX
installation, Android runtime, or GLES behavior.

The observed Linux object SHA-256 was
`23e59e02f46cf8bdc7985f91c35ad5c971ca74d26dfee1714bbb04b7394b881d`.
The full Android GLES+Vulkan build still fails in other objects due to
native-window type differences. The separate Vulkan-only build also has two
unresolved platform GPU-information functions. No complete backend has been
linked or tested on Android; this patch does not fix Guest Vulkan results.
