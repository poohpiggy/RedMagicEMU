# gfxstream d7a encoder base dependencies: ARM64 object study

This computer-only study uses the existing locally examined gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`. It compiles the actual
`Stream.cpp`, `StreamSerializing.cpp`, and `AndroidSubAllocator.cpp` sources
listed in `guest/android-emu/Android.bp`, then combines their objects with the
previously built encoder relocatable solely to inspect unresolved symbols.
It executes none of those components, and does not start a Guest, access a
device, or modify firmware, a Guest file, or an imported package.

The upstream source is not included here. Its original Android Open Source
Project copyright notices and Apache-2.0 license, and all other upstream
notices and licenses, must be preserved when using or redistributing it. The
existing source tree contains previous diagnostic changes and the encoder uses
previously generated headers; this is not a clean or self-contained release
build. The three sources use the same NDK r28c ARM64/API 35 target, platform
macros and include ordering as the documented encoder build, plus their own
`Android.bp` flags `LOG_TAG="androidemu"`, `-Wno-missing-field-initializers`,
`-fvisibility=default`, and `-fstrict-aliasing`. This hand-built configuration
is not a full Android Soong build.

Reproduce against already available local inputs, without downloading anything:

```sh
python3 tools/diagnostics/gfxstream-d7a-base-link/build.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --generated /var/tmp/rme-gfxstream-build.hTnuwsWV/build-guest \
  --aosp /var/tmp/rme-mesa-android-6ekppv9b \
  --ndk /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c \
  --encoder-dir /var/tmp/rme-vk-encoder-unified-6uu4ltgk
```

The script verifies the expected encoder-object SHA-256 and its 122 distinct
undefined symbols before compiling. It checks the platform header symlinks,
records the commands and observed header/source/compiler hashes, uses a new
temporary output directory, and checks the three objects are AArch64 REL. It
then uses `ld.lld -r` and `llvm-nm` to compute the exact set difference.
This is a **relocatable-object merge, not a shared-library link**; its undefined
symbols are not resolved by a real Android linker or loaded on Android.

Two independent runs produced matching inputs, object hashes and merged
object SHA-256 `0bbf21e731f1c9a2bfdaf7c2690e68dd6ac0d453b354ac37c2918225a010cf4c`.
Fourteen of the encoder's original 122 undefined symbols were resolved by the
three real objects. Those objects add 25 different undefined symbols, so the
merged result has **133**, not 108, undefined symbols; new dependencies include
two `MemStream` functions and C++ runtime symbols. The full resolved and new
symbol sets are saved in `resolved.tsv` and `introduced.txt`. No Mesa Android
stub headers were observed for these three objects; the encoder input still
uses two hardware stubs, and its Android 16 ABI is unverified.

These sources are not safe to expose directly to untrusted Guest input. Static
inspection found that `SubAllocator` range checks only log errors, basic Stream
reads do not consistently propagate short reads, `loadBufferRaw` has no target
capacity parameter, and `loadStringArray` does not bound its input count. None
of those paths were executed or repaired here. Real per-client validation,
resource ownership, transport, the remaining dependencies and the actual Vulkan
client/backend are still absent; no part of this result proves Guest Vulkan or
GPU execution works.
