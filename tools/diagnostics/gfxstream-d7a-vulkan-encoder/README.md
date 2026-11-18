# Android ARM64 Vulkan encoder build study

This is a computer-only build study for the locally examined gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`. It compiles the 18 Vulkan encoder
translation units listed in that revision's `guest/vulkan_enc/Android.bp` using
one common compiler argument list and include order. It does not build a Vulkan
client, communicate with a Guest or GPU, or change Android, EMU runtime files,
the original firmware or an imported package. The source tree, generated headers,
NDK and AOSP inputs must already exist; the script downloads nothing.

The source and upstream `LICENSE` remain in the supplied external source tree;
their copyright, notices and licenses must be preserved when using or
redistributing them. The locally available source includes earlier diagnostic
modifications, and the generated headers come from a prior host build; this is
not a clean-upstream or self-contained build. Input source, headers, compiler,
Android.bp and object SHA-256 values are recorded in each build directory.

For the existing local inputs, reproduce the old-header failure first:

```sh
python3 tools/diagnostics/gfxstream-d7a-vulkan-encoder/build.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --generated /var/tmp/rme-gfxstream-build.hTnuwsWV/build-guest \
  --aosp /var/tmp/rme-mesa-android-6ekppv9b \
  --ndk /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c --baseline
```

The baseline expects both `ResourceTracker.cpp` and `AndroidHardwareBuffer.cpp`
to fail with missing `AHARDWAREBUFFER_FORMAT_R8_UNORM` in the fixed-version Mesa
stub. Omit `--baseline` to build all 18 objects. The script creates a fresh
`/var/tmp/rme-vk-encoder-unified-*` directory and refuses to silently substitute
the older hardware-buffer headers. Each compilation uses the same NDK r28c API
35 target, macros and include ordering. A temporary include directory points to
the existing NDK `android/hardware_buffer.h`, `android/data_space.h` and
`android/rect.h` and the existing AOSP Android 16
`vndk/hardware_buffer.h`, as well as AOSP's existing `system/graphics*` headers,
all ahead of Mesa's existing `android_stub` path. No replacement header or
fabricated Vulkan feature is supplied.

For each file, the output records the exact command, compiler log, full `-MD`
dependency list, source and object hashes. `inputs.sha256` records all observed
input file hashes, including the selected real hardware-buffer headers.
`hardware-buffer-headers.txt` records their actual dependency paths;
`mesa-stub-headers.txt` records all remaining same-source Mesa stub headers.
These are diagnostic headers and their Android 16 ABI/semantics have not been
validated. Their use, the generated headers, and the source modifications must
be resolved before running or distributing a client.

All objects are checked for AArch64 relocatable ELF. A subsequent `ld.lld -r`
groups objects solely to inspect unresolved symbols. It is not a shared-library
link, cannot establish that the C/C++ and Android runtime dependencies resolve,
and does not implement VirtGpu, GoldfishAddressSpace or the EMU transport.
`undefined-symbols.txt` lists raw `llvm-nm -u` output;
`undefined-classified.tsv` is a conservative name-based classification for
triage, not proof of linkage or completeness. Both the number of raw output
lines and the number of distinct names are reported separately.

On this workspace, two independent complete builds produced 18 identical
objects, identical observed input hashes, and the same merged relocatable SHA-256
`65650c14dce86ec3740c857b933102504cceab44660c06f4a59324da08877c2f`.
They still used two Mesa stub headers, `hardware/hardware.h` and
`hardware/hwvulkan.h`, and had 122 undefined lines and 122 distinct names
after `-r`. Heuristic categories were 36 project/Mesa, 84 C/C++
or compiler runtime, and 2 Android platform symbols. The results are computer
build evidence only; Android, real GPU, the Guest and the actual loader have
not been tested.
