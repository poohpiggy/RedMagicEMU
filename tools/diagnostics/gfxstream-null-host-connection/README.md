# gfxstream Vulkan missing host connection

This is a research patch for the locally examined gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`. Its Vulkan client macro obtains
`HostConnection` and calls `vkEncoder()` before checking the result. If the
connection is null, the process may crash. The one-line patch instead follows
the existing missing-encoder error/return path for a missing connection too.

The source is not included here. It contains Android Open Source Project
copyright and Apache-2.0 notices; preserve those and all other upstream licenses
when applying or redistributing this patch. The examined local device source
contains earlier TRACE diagnostics and has SHA-256
`6b53405174b89935f32086e6529d2e6cf65fe7666e18ecbcf50e522d195ec045`.
The patch changes only the indicated macro line in a temporary copy and is
applied with `--fuzz=0`.

`run.py` extracts the actual macro from the supplied source and compiles it
with fake host-connection dependencies. It exercises missing connection,
missing encoder and normal operation in functions returning an integer, a
pointer and void. Each case runs in a separate process. On the examined source,
the three missing-connection cases crash before the patch; all nine cases pass
with it. This tests the real macro but not the complete client or GPU backend.

Example, using the existing local NDK host clang and source:

```sh
python3 tools/diagnostics/gfxstream-null-host-connection/run.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --patch tools/diagnostics/gfxstream-null-host-connection/fix.patch \
  --cxx /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ \
  --compiler-arg=-nostdlib++
```

Omit `--patch` to reproduce the three expected failures. No files in the supplied
source tree are changed. The Python script needs `patch`, a host C++ compiler,
and a Linux environment.

The patched Android ARM64 object can also be compiled in a temporary directory
with the previously generated headers and actual NDK/AOSP headers:

```sh
sh tools/diagnostics/gfxstream-null-host-connection/compile-android.sh \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/build-guest \
  /var/tmp/rme-mesa-android-6ekppv9b \
  /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
```

The script checks the compiler dependency list for Mesa `android_stub`
headers and fails if any are used. These pre-existing external source,
generated and toolchain directories are not included here. The resulting
object is not a linked client. This patch is not integrated into EMU and has
not been tested on Android, against a real service, or in Guest Vulkan.
