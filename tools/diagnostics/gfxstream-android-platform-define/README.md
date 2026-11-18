# gfxstream Android platform build definition

This is a build-configuration correction for the fixed gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`, not an EMU integration. The
upstream source remains in its separate research tree, with its original
copyright and licenses. No upstream source or safety check is changed here.

NDK clang defines `__ANDROID__` and `__linux__`, but not `ANDROID`. The fixed
gfxstream source tests `ANDROID` to select its existing Android native-window
branches. Without that build definition, the Android cross-build selects the
Linux `uint32_t` or X11 `Drawable` branches and fails when those types meet the
NDK's `ANativeWindow *`. The patch adds `-DANDROID` to C and C++ **only in the
Android cross file**; it does not alter desktop Linux configuration.

## Offline verification

The following reuses the fixed source, compiler databases and tools already on
this computer. It downloads nothing, runs no GLES or Vulkan implementation and
does not access a device:

```sh
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-android-platform-define/check.py \
  --source /var/tmp/rme-gfxstream-d7a-android-glesvk.9Zt5VCsh/source \
  --android-build /var/tmp/rme-gfxstream-d7a-android-glesvk.9Zt5VCsh/build \
  --linux-build /var/tmp/rme-gfxstream-d7a-android-glesvk.9Zt5VCsh/linux-build2 \
  --cross /var/tmp/rme-gfxstream-android-config.cj2YQmgw/cross.ini
```

The script verifies fixed input hashes and requires each of the original
`DisplaySurfaceGl.cpp`, `EmulationGl.cpp` and `EglOsApi_egl.cpp` object builds
to fail with its specific native-window type mismatch. It applies the patch to
a temporary cross file and recompiles those exact objects with the resulting
C++ flag. A desktop Linux compile-time type check confirms the unchanged Linux
header branch still uses `uint32_t`.

A separate fresh source copy at `/var/tmp/rme-d7a-android-platform.zk1aiJOj`
was configured with both this cross-file patch and the preceding
[`GLDispatch` include-guard patch](../gfxstream-android-gldispatch/README.md).
Its Meson/Ninja build successfully compiled those three objects and the fixed
`GLDispatch.cpp`; configure and object logs remain in that directory. This is
not a complete backend build. In a separate original-object comparison,
`FrameBuffer.cpp` moved past the original window-type conflict after the flag
was added but then failed because `cutils/native_handle.h` was unavailable.
Other errors, complete linking, Android runtime and Guest Vulkan are not
validated; no behavior of real GLES backing has been checked.
