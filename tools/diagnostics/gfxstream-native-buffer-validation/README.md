# Native-buffer image preflight

The examined d7a `ResourceTracker::on_vkCreateImage` eventually translates a
`VkNativeBufferANDROID` using the active gfxstream gralloc helper. A zero host
handle is invalid, but the original path could continue toward encoding it.
The patch checks the real helper's result before image creation: missing
connection/helper returns `VK_ERROR_DEVICE_LOST`, and a null native handle or
zero host handle returns `VK_ERROR_INVALID_EXTERNAL_HANDLE`. It does not invent
a host ID, import a buffer, alter a Guest, or replace the host's validation.

The CPU-only test extracts the real entry checkpoint and supplies a fake
gralloc helper. The unpatched version fails; the patched version rejects the
failure cases while ordinary images and a fake nonzero helper result proceed:

```sh
python3 tools/diagnostics/gfxstream-native-buffer-validation/run.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --cxx /var/tmp/rme-gfxstream-build.hTnuwsWV/bin/cxx \
  --patch tools/diagnostics/gfxstream-native-buffer-validation/fix.patch
```

The encoder build applies this patch to a copy by default. The complete encoder
was rebuilt for Android ARM64 and strictly linked offline. That does not prove
the Guest gralloc can supply a valid host ColorBuffer, that a nonzero ID exists
in the host's table, or that any image can be imported or rendered.

`check-layout.sh NDK SOURCE AOSP_HEADERS AOSP16_NATIVE_BUFFER_HEADER OUTPUT`
compile-checks the ARM64 d7a spec 9 header against the official Android 16
spec 11 header: common fields through `usage3` have matching offsets and the
spec 11 `ahb` field is a trailing addition. This is a struct-prefix check only.
The examined encoder reports native-buffer spec 7 and its wire marshaller does
not serialize `usage3` or `ahb`; matching offsets do not establish spec 9/11
support or a valid shared resource.
