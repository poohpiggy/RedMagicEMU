# Host-local Android Vulkan surface

`native/vulkanbridge/host_surface.c` owns an EMU host window reference and
creates a Vulkan surface with the **actual unboxed host instance corresponding
to the Guest**. It resolves `vkCreateAndroidSurfaceKHR` and
`vkDestroySurfaceKHR` with that instance and destroys the surface with the
same instance. The caller must ensure both surface extensions were enabled
when creating the instance, keep the instance alive and externally synchronize
object use. The wrapper constructor is for a trusted EMU host window owner;
no Guest pointer or IPC message is accepted by this interface.

The CPU test uses fake Vulkan entrypoints and a fake native window. It checks
two different instance identities, failed symbol lookup, driver error
propagation and window reference ownership. No GPU or phone is touched.

```sh
CC=/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang \
  sh tools/diagnostics/gfxstream-host-android-surface/test.sh
/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang \
  -std=c11 -Wall -Wextra -Werror -DVK_USE_PLATFORM_ANDROID_KHR \
  -c native/vulkanbridge/host_surface.c -o /var/tmp/rme-host-android-surface.o
```

This helper is not wired to the renderer or the decoder, does not verify its
caller's instance provenance or enabled extensions, and has not created an
actual surface. Android WSI remains hidden. Cross-compiling an object does not
establish runtime support or a rendered Guest frame.
