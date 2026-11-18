# Candidate JNI Surface ownership

`surface_jni.c` is a same-process JNI window slot for a future separately
owned Vulkan Surface. It obtains an `ANativeWindow` reference from a Java
`Surface`, serializes replacement and release, and returns failure when a new
surface cannot be converted. A conversion failure also clears the previous
window because it might have been destroyed. The native-only acquire function
returns another reference through `RmeVkHostWindow`; it does not expose a raw
pointer to the Guest or a different process.

This is an isolated candidate, not connected to a SurfaceView, service,
backend build, or package. The Java declaration does not load the candidate
library. A retained window reference does **not** keep the Surface usable
after Android destroys it. Before UI wiring, the caller must synchronously
stop presentation, destroy each matching swapchain and `VkSurfaceKHR`, release
all consumer references, and only then return from `surfaceDestroyed`.
The actual Guest-instance consumer and this teardown handshake do not exist
yet. No UI or product caller may invoke this JNI entry before that work.

```sh
sh /mnt/e/RedMagicEMU/native/vulkanbridge/tests/test_surface_jni.sh \
  /var/tmp/rme-gfxstream-build.hTnuwsWV \
  /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c \
  /var/tmp/rme-pipe-channel.w3wvezNf
```

The test uses synthetic window/JNI functions and the existing host-window
wrapper. It checks acquisition, replacement, conversion failure and reference
release, including that a consumer reference outlives the slot. The script
also uses the Android NDK ARM64 API 26 toolchain to strictly link both C files
against the NDK `libandroid` link stub and checks the ELF architecture, dynamic
dependencies, and exports. It first demonstrates the expected link failure
without `libandroid`. The output is written only to the specified temporary
directory; it is not loaded, copied into the app, installed, or run.

For the pinned NDK the resulting candidate depends on `libandroid.so`,
`libc.so` and `libdl.so`, and exports only
`Java_app_aemu_core_VulkanSurface_nativeUpdate`. Internal window/surface helper
symbols are hidden; a decoder in a different shared library cannot directly
call them. A future integrated backend needs an explicit in-process link or a
deliberately designed interface. This does not create a real Surface, test the
Android lifecycle, or access a device.

The headers expose the C implementation to a C++ backend using C linkage.
`test_surface_cpp_link.sh` compiles the real C helpers separately, calls them
from a C++ probe with substitute native windows, and checks release behavior.
It then strictly links the C helpers and the C++ probe together for Android
ARM64 against `libandroid`:

```sh
sh native/vulkanbridge/tests/test_surface_cpp_link.sh \
  /var/tmp/rme-gfxstream-build.hTnuwsWV \
  /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c \
  /var/tmp/existing-output-directory
```

Before the header fix the C++ probe failed to link against the C objects; the
checks above pass after it. The probe is not the gfxstream decoder and neither
link is evidence of runtime presentation or lifecycle integration.
