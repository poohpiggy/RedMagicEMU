# Android surface extensions on the Guest's host instance

This is an offline candidate patch against fixed gfxstream d7a source. If an
Android-hosted Guest explicitly asks for `VK_KHR_android_surface`, it checks
the real host driver's globally enumerated instance extensions and requires
both `VK_KHR_android_surface` and `VK_KHR_surface`. It adds the surface
dependency to that same Guest host instance, deduplicates extension names and
propagates enumeration errors. A compute-only instance does not run the new
enumeration. The check does not attempt to use extensions supplied only by a
Vulkan layer.

The unpatched guest and host filters already forward an explicit Android
surface request. However, the Guest ResourceTracker allowlist and the current
EMU extension gate do not expose that request to applications. The host has
no `vkCreateAndroidSurfaceKHR` dispatch entry. This patch does not enable the
extension, create a surface, connect the renderer, produce a frame, or alter
the Guest/phone. In particular, a surface from FrameBuffer's independent
VkInstance cannot be reused for the Guest's device.

The CPU harness extracts the actual `on_vkCreateInstance` prefix and instance
extension methods. A fake Vulkan extension enumerator and fake create sink
exercise deduplication, both missing-extension cases, enumeration errors and
the compute-only case. The original code fails the first assertion; the patch
passes. It does not execute a Vulkan driver or exercise the full instance
creation function. The source copy contains existing TRACE instrumentation and
is not claimed to be pristine upstream source.

```sh
SOURCE=/var/tmp/rme-gfxstream-build.hTnuwsWV/src
python3 tools/diagnostics/gfxstream-instance-android-surface/make_patch.py "$SOURCE" /var/tmp/rme-instance-surface-rebuilt.patch
cmp /var/tmp/rme-instance-surface-rebuilt.patch tools/diagnostics/gfxstream-instance-android-surface/fix.patch
python3 tools/diagnostics/gfxstream-instance-android-surface/run.py "$SOURCE"  # expected failure, requires a host C++17 compiler
python3 tools/diagnostics/gfxstream-instance-android-surface/run.py "$SOURCE" --patch tools/diagnostics/gfxstream-instance-android-surface/fix.patch
python3 tools/diagnostics/gfxstream-instance-android-surface/compile_android.py "$SOURCE" /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json /var/tmp/rme-instance-surface-android-new
```

The full patched `VkDecoderGlobalState.cpp` was cross-compiled as an ARM64
Android object with the existing compile database. That is not a linked or
runtime-tested backend. The patch has not been integrated or installed.
