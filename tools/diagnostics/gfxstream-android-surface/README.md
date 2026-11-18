# Isolated Android Vulkan display-surface candidate

The fixed upstream `DisplaySurfaceVk::create` supports Win32 but aborts on
Android. The isolated patch adds an Android `ANativeWindow` path that obtains
`vkCreateAndroidSurfaceKHR` from the actual instance's Vulkan dispatch and
returns failure when the function, window, or Vulkan creation is unavailable.
The existing destructor destroys the resulting `VkSurfaceKHR`. It does not
create or use a GLES context.

The patch also enables `VK_KHR_android_surface` only when the enumerated
instance extensions contain both it and `VK_KHR_surface`. If native swapchain
is requested without both extensions, initialization fails explicitly. The
existing generic surface selection and merged physical-device checks remain in
place. The source patcher accepts only the pinned upstream/merged hashes and
writes copies into the specified output directory.

The isolated build also patches the fixed `FrameBuffer::setupSubWindow`
Vulkan display-surface failure: it logs the error, releases the newly created
subwindow, clears native-window references and lets the existing false result
reach the caller. The GLES failure branch is unchanged. A test extracts the
upstream failure block, first reproduces its abort with substitutes, and then
verifies release and failure for the patched block; it does not run the full
renderer or a live Vulkan window.

This cleanup is **not a complete failure-handling fix**. The upstream function
still broadcasts `sInitialized=true` after failure; `waitUntilInitialized()`
cannot report an error, and the upstream void window setup caller ignores the
show result. `DisplayVk::post` also reports success with no bound surface.
Those paths require a real error propagation or a verified stop-before-Guest
connection strategy before this candidate can be integrated.

```sh
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-android-surface/verify.py \
  /var/tmp/rme-gfxstream-build.hTnuwsWV \
  /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer \
  /var/tmp/rme-d7a-vk-merged-android.IFuEeE2h/source/host/vulkan/VkCommonOperations.cpp \
  /var/tmp/rme-pipe-channel.w3wvezNf/android-wsi
```

The verification compiles the patched files with the actual Android build
flags, compiles the display surface code for the desktop against synthetic
Vulkan callbacks, and tests both extension combinations and resource cleanup.
It does not open a real driver, create an Android Surface, or run a renderer.

```sh
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-androidpipe-channel/verify_headless.py \
  /var/tmp/rme-gfxstream-build.hTnuwsWV \
  /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer \
  /var/tmp/rme-pipe-channel.w3wvezNf/android-headless

python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-androidpipe-channel/relink_android.py \
  --without-uuid \
  --gpuinfo-dir /var/tmp/rme-pipe-channel.w3wvezNf/gpuinfo-provider \
  --android-wsi-dir /var/tmp/rme-pipe-channel.w3wvezNf/android-wsi \
  --headless-dir /var/tmp/rme-pipe-channel.w3wvezNf/android-headless \
  /var/tmp/rme-gfxstream-build.hTnuwsWV \
  /var/tmp/rme-aemu-android-wrapper.53oSJ2Tb/build-shared \
  /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI \
  /var/tmp/rme-d7a-vk-merged-android.IFuEeE2h/build/host/vulkan/liblib_vulkan_server.a.p/VkCommonOperations.cpp.o \
  /var/tmp/rme-pipe-channel.w3wvezNf
```

The strict link is expected to fail with unresolved
`isVulkanSafeToUseNative()`. The candidate has not been linked into the app or
tested on a device. It does not establish Guest image delivery, Android Surface
lifecycle integration, or a functioning Guest Vulkan swapchain.
