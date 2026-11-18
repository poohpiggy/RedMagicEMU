# Android acquire-fence failure handling

This rebuildable patch targets the locally examined d7a gfxstream client. Its
Android `vkAcquireImageANDROID` encoder waits on a native fence and owns the
passed fence fd, but originally ignored a timeout or wait error and continued
encoding the acquire as though the fence had signalled. The caller is Android
16's Vulkan swapchain loader, which explicitly expects the driver to close its
duplicated fence on both success and failure.

The patch returns `VK_ERROR_DEVICE_LOST` on any failed wait, closes the fd once,
does not enqueue the acquire packet, and releases the encoder lock exactly when
the original function acquired it. A successfully waited fd is represented as
`-1` in the packet instead of forwarding a closed guest descriptor. The wait
duration remains the original 3000 ms. No fake sync success is introduced.

`run.py` extracts the actual helper and acquire checkpoint from the specified
source; it compiles them against CPU-only substitutes. It exercises no-fence,
successful wait, failed wait, and both encoder lock paths; the substitute fd is
an integer and no real device, fence or memory mapping is accessed. On the
unpatched source the failed-wait case fails. With this patch all cases pass:

```sh
python3 tools/diagnostics/gfxstream-acquire-fence/run.py \
  --source SOURCE --cxx HOST_CXX \
  --patch tools/diagnostics/gfxstream-acquire-fence/fix.patch
```

`native/vulkanbridge/client/rebuild_encoder_candidates.py` applies the patch by
default to a source copy. The 18 actual
encoder translation units and the complete diagnostic client have compiled and
strictly linked for Android ARM64. This is not an actual fence,
Guest, swapchain or GPU test. It does not repair missing Android window WSI,
the host decoder, or the verified linker namespace/C++ runtime constraints.
