# Android Vulkan GPU information candidate

This isolated candidate implements the upstream `getGpuInfoListNative` symbol
using Android's Vulkan loader. It creates a Vulkan 1.0 instance and enumerates
physical-device properties; it does not choose a renderer device, establish a
GPU safety decision, or start a renderer. It has not been integrated into the
EMU product or run against a real device.

In this fixed upstream revision, `host/gl/gl-host-common/Android.bp` explicitly
compiles `opengl/NativeGpuInfo_linux.cpp` for its Android build. That source's
`isVulkanSafeToUseNative()` returns `true` unconditionally and describes Linux
as having no Vulkan blacklist. This is the upstream build's behavior, not an
Android driver-specific safety decision. The candidate does not substitute or
invoke it: the project requires an actual safety decision rather than a fixed
success result.

The `GpuInfo` entries contain the Vulkan vendor ID, device ID, device name,
and raw Vulkan driver-version integer. This is not complete GLES driver or
blacklist information: GLES renderer and revision remain unknown, and no
entry is claimed as the renderer-selected GPU. Vulkan's vendor-specific raw
driver integer is kept in `version`, not misrepresented as a revision.

The upstream entry point returns `void`. The candidate records an independent
`GpuInfoResult` and logs failures; callers cannot infer success from an empty
list. The native adapter serializes queries and records the most recent result
with its output-list address; another list replaces it and a mismatch fails
closed. The candidate Android renderer startup waits for the upstream GPU-info
worker via `globalGpuInfoList()` and checks the result for that exact list
before consulting the existing blacklist. Its isolated test uses substitutes;
this has not been tested with a real renderer. **Before any product integration,
the incomplete data must also be evaluated against existing safety rules.**
The Vulkan safety provider and a binding between the checked and selected
device remain unresolved. The candidate must not be used to bypass them.

With the pinned local build inputs, run the synthetic tests and cross-compile
the ARM64 objects (the test uses synthetic Vulkan functions and does not load a
real driver):

```sh
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-android-gpuinfo/verify.py \
  /var/tmp/rme-gfxstream-build.hTnuwsWV \
  /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer \
  /var/tmp/rme-pipe-channel.w3wvezNf/gpuinfo-provider
```

Expected markers: `SYNTHETIC_VULKAN_GPUINFO_ENUMERATION_OK` and
`ANDROID_ARM64_PROVIDER_OBJECTS_OK; SAFETY_PROVIDER_NOT_IMPLEMENTED`.
The script requires the pinned GPU information header and uses the existing
Android build's compile command and NDK. Temporary paths are local build
inputs, not permanent dependencies.

To independently check the strict Android backend link with the previously
verified non-UUID device-selection object and these candidate objects, first
build the headless candidate using the `verify_headless.py` command documented
in `../gfxstream-android-surface/README.md`, then run:

```sh
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-androidpipe-channel/relink_android.py \
  --without-uuid --gpuinfo-dir /var/tmp/rme-pipe-channel.w3wvezNf/gpuinfo-provider \
  --headless-dir /var/tmp/rme-pipe-channel.w3wvezNf/android-headless \
  /var/tmp/rme-gfxstream-build.hTnuwsWV \
  /var/tmp/rme-aemu-android-wrapper.53oSJ2Tb/build-shared \
  /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI \
  /var/tmp/rme-d7a-vk-merged-android.IFuEeE2h/build/host/vulkan/liblib_vulkan_server.a.p/VkCommonOperations.cpp.o \
  /var/tmp/rme-pipe-channel.w3wvezNf
```

The required outcome is a failed strict link with the sole unresolved symbol
`isVulkanSafeToUseNative()`. This reports symbol resolution only; it does not
validate device safety, renderer initialization, or Guest rendering.

## Android Vulkan-only backend with the preflight

`preflight.cpp` fills the missing Android entry point by running the existing
fixed 64-word Vulkan shader and verifying its readback through the Android
loader. The existing upstream blacklist and renderer device checks still run.
The renderer selects the probe's device and driver UUID pair. Both UUIDs come
from the same physical device that executed the shader. This check covers
startup compute only. External memory, synchronization, surface presentation
and Guest command transport still require runtime verification.

Build the paired ARM64 backend from the existing local pinned inputs:

```sh
REPO=/mnt/e/RedMagicEMU
TOOLS=/var/tmp/rme-gfxstream-build.hTnuwsWV
BUILD=/var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV
PAIR=/var/tmp/rme-negotiated-combined-257akfbl
OUT=$(mktemp -d /var/tmp/rme-vulkan-backend.XXXXXXXX)
python3 "$REPO/tools/diagnostics/gfxstream-swapchain-generator/link_vulkanonly.py" \
  "$TOOLS" "$BUILD/build-fixed" "$PAIR/host/VkDecoder.o" "$PAIR/components" \
  /var/tmp/rme-integrated-create-link-w1y5ljq_ "$BUILD/gpuinfo" "$OUT/backend" \
  --render-thread "$PAIR/renderthread/RenderThread.o" --with-android-preflight
```

The command runs the preflight unit cases and a real llvmpipe rejection check
before the strict link, then prints the library hash. The library and ELF
report are in `$OUT/backend`. The backend still needs the matching Android
aemu libraries and EMU process integration. This build does not deploy an APK
or alter the firmware. Omit `--with-android-preflight` to reproduce the missing
provider failure in a new output directory.
