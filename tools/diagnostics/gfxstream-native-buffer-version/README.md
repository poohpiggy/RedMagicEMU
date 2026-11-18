# Native-buffer version consistency

The examined d7a ResourceTracker reports `VK_ANDROID_native_buffer` spec 7.
Its device-extension enumerator copied Mesa's static entry, which reports spec
8, thereby reporting a higher version than this encoder actually provides.
This patch caps that one entry at 7 and leaves other entries unchanged.

The CPU-only regression extracts the actual enumeration function. The original
source reports 8 and fails; the patched function reports 7 and preserves an
unrelated extension:

```sh
python3 tools/diagnostics/gfxstream-native-buffer-version/run.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --cxx /var/tmp/rme-gfxstream-build.hTnuwsWV/bin/cxx \
  --patch tools/diagnostics/gfxstream-native-buffer-version/fix.patch
```

`compile_android_device.sh` applies the patch to a temporary copy, and the
patched Android ARM64 device object strictly linked offline. This does not
create a native buffer, prove WSI support, or advertise swapchain support.
