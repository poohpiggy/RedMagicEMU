# gfxstream d7a MemStream and strict Android link study

This computer-only experiment compiles the real `MemStream.cpp` from the locally
available gfxstream revision `d7a33801fb557cebd4f8564a8af431669b3f471d`, adds
its object to the previously built encoder-plus-base relocatable object, and
attempts an Android ARM64/API 35 shared-library link with `--no-undefined`.
It executes no compiled code, opens no GPU device, and does not change an EMU
runtime, a Guest file, a firmware image or an imported package.

The source is not distributed here. Preserve its Android Open Source Project
copyright and Apache-2.0 license, along with all other upstream notices and
licenses, when using or redistributing it. This build uses a local source tree
with earlier diagnostics, generated headers and previously built objects; it is
not a self-contained or clean upstream build. The baseline encoder still uses
two Mesa hardware compatibility headers whose Android 16 ABI is unverified.

Run with the existing local inputs; the script downloads nothing and creates a
new `/var/tmp/rme-vk-memstream-*` output directory for every run:

```sh
python3 tools/diagnostics/gfxstream-d7a-memstream/build.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --generated /var/tmp/rme-gfxstream-build.hTnuwsWV/build-guest \
  --aosp /var/tmp/rme-mesa-android-6ekppv9b \
  --ndk /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c \
  --encoder-dir /var/tmp/rme-vk-encoder-unified-6uu4ltgk \
  --base-dir /var/tmp/rme-vk-base-link-g6jfrbcp
```

The script checks the recorded baseline object and source SHA-256 values, the
source entry in `Android.bp`, target NDK/AOSP header paths and the two missing
MemStream symbols. It first runs the actual Android linker with the original
object, NDK `libc++_shared.so`, API 35 NDK `liblog.so` and `libandroid.so`,
and `--no-undefined`. It then compiles the real source, checks AArch64 REL,
uses `ld.lld -r` to inspect the resulting symbol set, and retries the same strict
shared-library link. The NDK platform libraries are link interfaces, not proof
that symbols or namespaces are available on the target phone. Link commands,
errors, actual compile dependencies and SHA-256 input hashes are recorded.

Two independent runs had the same inputs and output hashes. The base object
had 133 undefined symbols, and the strict link reported 25 distinct undefined
symbols, including `MemStream::load` and `MemStream::save`. Adding the real
MemStream object resolved exactly those two without introducing new undefined
symbols: `llvm-nm` then showed 131, and the strict link still failed with 23.
The object SHA-256 was
`80bf395d6280af39f5cef059af0175aabf41af9df07c6fd144c1c0940fa3d371`, and the
combined **relocatable**, not shared, object's SHA-256 was
`e4477501de21dbef459498600796f90c5d953ed32dcd95f22c845d8966beb9e5`.
No shared library was generated. Outstanding errors include VirtGpu,
GoldfishAddressSpace, HealthMonitor, WorkPool, tracing and Mesa functions.

This component must not be exposed directly to untrusted Guest input: static
source inspection shows `MemStream::load` ignores the result of `loadBuffer`,
while that helper resizes from a length read from the stream without a bound.
These paths were not executed or repaired here. The full Vulkan client,
application loader, resource ownership and host communication are still absent;
this experiment does not demonstrate Guest Vulkan or GPU execution.
