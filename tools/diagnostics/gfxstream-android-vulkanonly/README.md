# Isolated Android Vulkan-only build

In the fixed gfxstream revision, Android builds with GLES disabled failed to
compile: `GlesCompat.h` declared `EGLNativeWindowType` as a 32-bit integer,
while the actual Android `FBNativeWindowType` is `ANativeWindow*`. The isolated
patch uses the native pointer type on Android only; other platforms and GLES
builds are unchanged. The ARM64 type assertion fails before the patch and
passes afterward.

An offline `vulkan,composer` cross-build with the previously merged physical
device checks compiled all objects and reached a strict link. Without the GPU
information candidate it fails on `getGpuInfoListNative` and
`isVulkanSafeToUseNative`; with that candidate it still fails on the latter.
The independent GPU information source is incomplete for blacklist matching
and must not be treated as a safety decision. The build is not a usable
renderer. Vulkan-only Meson selects `NativeSubWindow_stub.cpp`, which does not
present frames; direct Guest-instance surface wiring remains separate.

Existing local run and audit:

```sh
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-android-vulkanonly/verify.py \
  /var/tmp/rme-gfxstream-build.hTnuwsWV \
  /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/source \
  /var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV/source \
  /var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV/build-fixed \
  /var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV/gpuinfo \
  /var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV/audit
```

The temporary `build-fixed.log` and `audit/strict-link.log` contain the actual
failed links. The script checks the fixed type, the exact merged-device source
hash and the strict unresolved-symbol set. It does not load a driver or access
a device. Temporary paths are local research inputs, not a distributable build.

To reconstruct the isolated source and build from the existing pinned inputs:

```sh
REPO=/mnt/e/RedMagicEMU
TOOLS=/var/tmp/rme-gfxstream-build.hTnuwsWV
BASE=/var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI
MERGED=/var/tmp/rme-d7a-vk-merged-android.IFuEeE2h/source/host/vulkan/VkCommonOperations.cpp
DEPS=/var/tmp/rme-aemu-android-wrapper.53oSJ2Tb/prefix-shared
OUT=$(mktemp -d /var/tmp/rme-vulkanonly-rebuild.XXXXXXXX)
cp -a "$BASE/source" "$OUT/source"
python3 "$REPO/tools/diagnostics/gfxstream-android-vulkanonly/apply_window_type.py" \
  "$BASE/source/host/GlesCompat.h" "$OUT/source/host/GlesCompat.h"
cp "$MERGED" "$OUT/source/host/vulkan/VkCommonOperations.cpp"
export PYTHONPATH="$TOOLS/sysroot/usr/lib/python3/dist-packages"
export LD_LIBRARY_PATH="$TOOLS/sysroot/usr/lib/x86_64-linux-gnu"
export PATH="$TOOLS/sysroot/usr/bin:$PATH"
export PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig"
"$TOOLS/sysroot/usr/bin/meson" setup "$OUT/build-fixed" "$OUT/source" \
  --cross-file "$BASE/cross-composer.ini" -Dgfxstream-build=host \
  -Ddecoders=vulkan,composer -Dplatforms= -Dwrap_mode=nodownload -Db_lundef=true
"$TOOLS/sysroot/usr/bin/ninja" -C "$OUT/build-fixed" -j4 -k0 host/libgfxstream_backend.so \
  > "$OUT/build-fixed.log" 2>&1 || true
python3 "$REPO/tools/diagnostics/gfxstream-android-gpuinfo/verify.py" \
  "$TOOLS" "$OUT/build-fixed" "$OUT/gpuinfo"
mkdir "$OUT/audit"
python3 "$REPO/tools/diagnostics/gfxstream-android-vulkanonly/verify.py" \
  "$TOOLS" "$BASE/source" "$OUT/source" "$OUT/build-fixed" "$OUT/gpuinfo" "$OUT/audit"
```
