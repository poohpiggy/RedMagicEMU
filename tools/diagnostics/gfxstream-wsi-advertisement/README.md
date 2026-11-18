# Incomplete Android WSI extension advertisement

The fixed d7a guest source merges host instance extensions and always adds its
Mesa swapchain device extension. Until the EMU implementation has working
Android swapchain acquisition and presentation, this can advertise
`VK_KHR_android_surface` and `VK_KHR_swapchain` even though those extensions
are not usable. This patch filters those two names while retaining other host
and Mesa extensions. It does not implement or emulate rendering.

The CPU-only test extracts the actual instance and device extension-discovery
functions and supplies a fake host reporting the two incomplete extensions and
unrelated ones. The original function fails; the patched function hides the
two names and preserves the others:

```sh
python3 tools/diagnostics/gfxstream-wsi-advertisement/run.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --cxx /var/tmp/rme-gfxstream-build.hTnuwsWV/bin/cxx \
  --patch tools/diagnostics/gfxstream-wsi-advertisement/fix.patch
```

Omit `--patch` to reproduce the expected failure. The client Android device
build script applies this patch to a temporary source copy. The test does not
run a Guest, driver, decoder, Android window or GPU. Remove this gate only after
the full Android surface/swapchain and real presentation path is implemented
and verified; it is not evidence of functional WSI.
