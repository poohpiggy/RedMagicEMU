# gfxstream d7a33801 Android backend build study

This records an offline cross-build of the fixed gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`. It is not linked into EMU or run on
Android. Upstream source and notices remain in the separate local source tree.

Starting from the isolated source and Android cross file with the prior
[GLX guard](../gfxstream-android-gldispatch/README.md) and
[`ANDROID` definition](../gfxstream-android-platform-define/README.md):

* `android-window-selection.patch` makes Meson select the existing
  `NativeSubWindow_android.cpp`, matching this revision's `host/Android.bp`.
* `aosp-header-path.patch` adds the locally available AOSP Android 16 libcutils
  include directory. Its real `cutils/native_handle.h` has SHA-256
  `af0277ffd3756c4c1047071742c362ef1b3f036552eefad98980266fc4660e9c`.
  No replacement implementation is provided.
* `ndk-android-link.patch` adds the NDK's normal `-landroid` link interface.
  Enabling `gles,vulkan,composer` also compiles and links the real existing
  renderControl decoder. Neither establishes Android runtime availability.

The original `FrameBuffer.cpp` object fails because the cutils header is not
found; with that AOSP include directory it compiles. Meson selects and compiles
the Android native-window source. The full 176-step build reaches its final
strict `--no-undefined` link, which fails only on
`getGpuInfoListNative(GpuInfoList*)` and `isVulkanSafeToUseNative()`. No GPU
information implementation, unconditional safety approval, or substitute has
been added. This is not a working backend or a Guest Vulkan fix.

Raw configuration and build logs, local patches and input hashes are in
`/var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/`:
`configure.log`/`build.log` are before the composer and NDK link additions;
`configure-composer.log`/`build-composer.log` contain the final result. The
earlier full Android build errors are recorded at
`/var/tmp/rme-gfxstream-d7a-android-glesvk.9Zt5VCsh/build.log`.

To check the recorded evidence and recompile the single FrameBuffer object:

```sh
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-d7a-android-backend/verify.py \
  --baseline /var/tmp/rme-d7a-android-platform.zk1aiJOj \
  --candidate /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI \
  --header-dir /var/tmp/rme-mesa-android-6ekppv9b/aosp-core/libcutils/include
```

To recreate the attempted full build without downloading dependencies:

```sh
REPO=/mnt/e/RedMagicEMU
BASE=/var/tmp/rme-d7a-android-platform.zk1aiJOj
TOOLS=/var/tmp/rme-gfxstream-build.hTnuwsWV
DEPS=/var/tmp/rme-aemu-android-wrapper.53oSJ2Tb/prefix-shared
OUT=$(mktemp -d /var/tmp/rme-d7a-backend-rebuild.XXXXXXXX)
cp -a "$BASE/source" "$OUT/source"
cp "$BASE/cross.ini" "$OUT/cross.ini"
patch -d "$OUT/source" -p1 < "$REPO/tools/diagnostics/gfxstream-d7a-android-backend/android-window-selection.patch"
patch -d "$OUT" -p1 < "$REPO/tools/diagnostics/gfxstream-d7a-android-backend/aosp-header-path.patch"
patch -d "$OUT" -p1 < "$REPO/tools/diagnostics/gfxstream-d7a-android-backend/ndk-android-link.patch"
export PYTHONPATH="$TOOLS/sysroot/usr/lib/python3/dist-packages"
export LD_LIBRARY_PATH="$TOOLS/sysroot/usr/lib/x86_64-linux-gnu"
export PATH="$TOOLS/sysroot/usr/bin:$PATH"
export PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig"
"$TOOLS/sysroot/usr/bin/meson" setup "$OUT/build" "$OUT/source" \
  --cross-file "$OUT/cross.ini" -Dgfxstream-build=host -Ddecoders=gles,vulkan,composer \
  -Dplatforms= -Dwrap_mode=nodownload -Db_lundef=true
"$TOOLS/sysroot/usr/bin/ninja" -C "$OUT/build" -j4 -k 0 host/libgfxstream_backend.so
```

The source and dependencies are temporary local research inputs, not a
self-contained source package. No GLES backing, hardware, or device execution
was tested.
