# Android surface-support dispatch diagnostic

The candidate public surface object begins with an `ANativeWindow *`. The
matching d7a Mesa `wsi_GetPhysicalDeviceSurfaceSupportKHR` instead interprets
the handle as `VkIcdSurfaceBase` and uses its first field as an index into a
WSI interface array. `run.py` extracts the complete actual Mesa function and
compiles it with a synthetic EMU-layout surface and substitute surrounding
objects. Without the route, the `supported` test exits with SIGSEGV (-11).

The independent route recognizes a surface using a caller-supplied registry
lookup, then forwards device, queue family and window to a caller-supplied
support query. If the surface is owned but no usable window/query exists, it
returns `VK_ERROR_SURFACE_LOST_KHR` and does not report support. An unowned
surface is left to the existing implementation. This is an integration seam,
not an implemented Android support query: callers must supply a genuine device,
queue and presentation-capability provider, and the callback must hold the
surface alive for its duration. The local test's provider is synthetic.

```sh
python3 tools/diagnostics/gfxstream-surface-support/run.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --cc /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang \
  --patched
```

Omit `--patched` to reproduce the SIGSEGV. Patched runs cover owned supported,
owned unsupported, missing provider, provider error and foreign surface. The
route also compiled separately with the NDK Android API 35 AArch64 compiler.

The public client now recognizes its own surface handles in the four core
surface queries and returns `VK_ERROR_SURFACE_LOST_KHR` until a real provider is
available; foreign handles still use Mesa. Its GIPA path returns the same public
wrappers only after the original extension lookup succeeds. The independent
callback route above is not integrated. The client test exercises direct and
GIPA dispatch with fake devices and callbacks; no Android surface, actual
device, GPU, Guest or firmware was tested, and no unimplemented extension
should be advertised based on this diagnostic. Additional surface-query
extensions need matching routing before they are enabled.
