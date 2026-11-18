# gfxstream d7a Android encoder Goldfish configuration

This computer-only study compares the locally examined gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`'s Android Vulkan encoder with
`GFXSTREAM_ENABLE_GUEST_GOLDFISH` set to 0 and 1. The fixed version's
`guest/Android.bp` sets it to 1 through `libgfxstream_guest_cc_defaults`;
the prior hand-built ARM64 encoder omitted it, so its preprocessor value was 0.
The upstream source is not redistributed here. Preserve its AOSP copyright,
Apache-2.0 license and all other applicable notices when using it.

The script verifies the baseline's source/object hashes, the 18-source Android.bp
file group, the default macro, and the NDK/AOSP hardware-buffer header targets.
It compiles every source into AArch64 REL using three configurations:

1. Previous diagnostic flags plus explicit macro 0, to check the prior baseline.
2. The same flags plus macro 1, to isolate the effect of that macro.
3. Macro 1 plus the Vulkan-specific preprocessor flags and warning/aliasing
   options listed in the same encoder Android.bp.

The third group is still a hand-built diagnostic, **not a complete Soong build**:
it uses API 35 NDK, locally generated headers, two Mesa hardware compatibility
headers and local source containing previous diagnostic changes. Android 16 ABI,
dynamic linking and runtime behavior are unverified. No source or firmware is
changed, and no compiled code is executed; in particular it does not open any
device node or run a Guest or GPU workload.

Run against existing local inputs; nothing is downloaded or overwritten:

```sh
python3 tools/diagnostics/gfxstream-d7a-goldfish-config/build.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --generated /var/tmp/rme-gfxstream-build.hTnuwsWV/build-guest \
  --aosp /var/tmp/rme-mesa-android-6ekppv9b \
  --ndk /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c \
  --encoder-dir /var/tmp/rme-vk-encoder-unified-6uu4ltgk --jobs 2
```

The script creates a fresh `/var/tmp/rme-vk-goldfish-config-*` directory,
saves all compilation commands, logs, dependency lists, input hashes and object
hashes, and preprocesses `ResourceTracker.cpp` under macro 0 and 1. Two complete
independent runs produced identical input and object manifests. All 18 macro-0
objects matched the previously recorded baseline. Only `ResourceTracker.o`
changed when toggling the macro; its macro-1 object added unresolved `open` and
`ioctl` calls and contains the `/dev/goldfish_sync` path. The preprocessing
check confirmed the Goldfish sync functions appear only with macro 1. Adding
the other Android.bp flags changed two additional objects
(`CommandBufferStagingStream` and `DescriptorSetVirtualization`); changed hashes
do not establish ABI or runtime behavior. All 54 outputs were verified as
AArch64 REL objects, not linked or run.

This difference matters: in the fixed source, when native Virtio sync is absent,
some fence/QSRI paths compiled with macro 0 may return success without assigning
an output fd. Macro 1 compiles the Goldfish sync path, but it requires an actual
Goldfish sync backend and some call sites do not check the queue-work result.
The presence of calls or an object file does not prove that device, transport,
FD ownership, errors or synchronization work. Neither configuration is an
integrated client or evidence of correct Guest Vulkan execution. The former
macro-0 objects must not be treated as a working Android synchronization path.
