# Isolated swapchain image metadata candidate

The pinned d7a decoder creates and destroys host swapchains directly without
retaining the device or the swapchain image creation parameters. The candidate
records device, flags, format, extent, array layers, usage, sharing mode and a
private copy of concurrent queue-family indices under the boxed swapchain. It
records only whether a `pNext` chain existed, never the borrowed pointer.
Normal swapchain destruction removes the record only for the owning device;
device teardown and global state reset remove their records as well.

`run.py` checks where the actual decoder hooks occur and extracts the candidate
methods from `VkDecoderGlobalState.cpp` into a fake-state CPU harness. The
original decoder fails because it does not track swapchain creation or
destruction. The candidate passes field preservation, array ownership,
device-scoped cleanup and exclusive-sharing checks. The harness does not run
the complete decoder, Vulkan loader, actual driver, or a GPU.

This is only the first step toward swapchain image registration. It does not
turn the metadata into `VkImageCreateInfo`, copy or interpret `pNext`, register
images or box their identities. Swapchain image enumeration still returns raw
driver handles. Present, snapshot recovery, client authorization and actual
surface teardown remain unresolved. The patch edits an auto-generated decoder;
it must be ported to its generator before a reproducible product build. Keep
WSI unavailable and do not integrate or deploy this candidate as-is.

With the existing local, TRACE-instrumented source and Android cross-build:

```sh
SOURCE=/var/tmp/rme-gfxstream-build.hTnuwsWV/src
python3 tools/diagnostics/gfxstream-swapchain-metadata/run.py "$SOURCE" # expected failure
python3 tools/diagnostics/gfxstream-swapchain-metadata/run.py "$SOURCE" --patch tools/diagnostics/gfxstream-swapchain-metadata/fix.patch
python3 tools/diagnostics/gfxstream-swapchain-metadata/compile_android.py "$SOURCE" /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json /var/tmp/rme-swapchain-meta-android-new --patch tools/diagnostics/gfxstream-swapchain-metadata/fix.patch
```

Both patched full decoder objects compile as Android ARM64 with the existing
build database. They have not been linked into a renderer or run on Android.
