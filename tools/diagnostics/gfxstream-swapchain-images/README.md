# Isolated swapchain image registration candidate

This patch applies after `../gfxstream-swapchain-metadata/fix.patch`. For the
limited case of an existing same-device swapchain with no creation `pNext`, no
swapchain creation flags, no snapshot mode, no emulated compressed format and
no conflicting raw image value, enumeration records image metadata and maps each
driver image to a stable boxed identity. It prechecks the full returned batch,
rolls back newly allocated boxes if allocation fails, and reuses boxes on later
enumerations. Both `VK_SUCCESS` and `VK_INCOMPLETE` are handled. A registration
failure changes the result to an error and emits an image count of zero, so the
decoder's existing response cannot serialize the returned raw image values.

The image metadata is removed and the box released on swapchain or device
cleanup. `destroyImageLocked` skips tracked swapchain-owned images: only the
driver's swapchain destruction owns those images. No `vkDestroyImage` is issued
by the new cleanup code. State clearing also releases the new boxes.

The baseline test first fails because the actual decoder does not register
returned images. `run.py` extracts the patched registration and image-destruction
methods and the actual result-handling statements from the decoder. Fake state,
handle manager and driver exercise partial and repeated enumeration, result
preservation, cross-device/raw conflicts, unsupported metadata, rollback and
cleanup. This is not a complete decoder execution or a GPU test.

**Do not integrate or expose this candidate.** Existing `mImageInfo` remains
globally keyed by raw image value. Even though registration rejects collisions
already present, a later ordinary image creation on another device can overwrite
an existing swapchain image's entry. The global box manager and cross-client
authorization also remain unresolved. This candidate does not implement real
Surface creation, actual image presentation, snapshot recovery, or a complete
WSI. The host decoder source is generated and the candidate must be ported to
its generator. No APK, device, driver or Guest has been accessed.

```sh
SOURCE=/var/tmp/rme-gfxstream-build.hTnuwsWV/src
python3 tools/diagnostics/gfxstream-swapchain-images/run.py "$SOURCE" # expected failure
python3 tools/diagnostics/gfxstream-swapchain-images/run.py "$SOURCE" --patch tools/diagnostics/gfxstream-swapchain-images/fix.patch
python3 tools/diagnostics/gfxstream-swapchain-metadata/compile_android.py "$SOURCE" /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json /var/tmp/rme-swapchain-images-android-new --patch tools/diagnostics/gfxstream-boxed-handle-collision/fix.patch --patch tools/diagnostics/gfxstream-boxed-handle-collision/type-key.patch --patch tools/diagnostics/gfxstream-image-view-device/fix.patch --patch tools/diagnostics/gfxstream-swapchain-metadata/fix.patch --patch tools/diagnostics/gfxstream-swapchain-images/fix.patch
```

The two complete patched decoder sources compile to Android ARM64 objects using
the pre-existing local build database. They have not been linked or executed.
