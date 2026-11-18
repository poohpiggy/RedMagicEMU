# gfxstream image-view device ownership candidate

The fixed d7a `on_vkCreateImageView` looks up its image using only the raw
`VkImage` value. The associated `ImageInfo` already records its owning
`VkDevice`, but the method did not check that it matched the requested device
before calling the driver. This matters when future swapchain image registration
encounters an image value belonging to a different device.

`fix.patch` checks the recorded device and returns `VK_ERROR_UNKNOWN` on a
mismatch before dispatch. It leaves same-device dispatch and the existing
missing-record result unchanged. This is an isolated candidate; it does not
register swapchain images, resolve raw-handle collisions, validate a Guest
client's authority, or produce frames. The WSI candidate remains incomplete.

`run.py` extracts the complete real method and tests it with an in-process fake
driver and fake state objects. The unpatched source fails case 2: a different
device reaches the fake driver. With the patch, same-device dispatch succeeds,
different-device input is rejected, and an unknown image retains its old error.
This does not invoke a real driver, GPU, Android device, or Guest.

```sh
SOURCE=/var/tmp/rme-gfxstream-build.hTnuwsWV/src
python3 tools/diagnostics/gfxstream-image-view-device/run.py "$SOURCE" # expected failure: case 2
python3 tools/diagnostics/gfxstream-image-view-device/run.py "$SOURCE" --patch tools/diagnostics/gfxstream-image-view-device/fix.patch
python3 tools/diagnostics/gfxstream-instance-android-surface/compile_android.py "$SOURCE" /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json /var/tmp/rme-view-device-android-new --patch tools/diagnostics/gfxstream-image-view-device/fix.patch
```

The full patched host source compiled into an Android ARM64 object using the
existing local compile database. It was not linked or run. Inputs include the
existing TRACE-instrumented source copy and local Android build dependencies.
