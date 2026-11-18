# d7a Vulkan device creation result

This is a research patch for gfxstream commit `d7a33801fb557cebd4f8564a8af431669b3f471d`.
It is not connected to RedMagicEMU, built into an APK, or tested on Android.
The original `createGlobalVkEmulation` discards the `vkCreateDevice` result
and checks a previous result. The patch stores the actual result so that the
existing error branch executes when device creation fails.

The regression copies an existing Linux host build and the fixed source into a
temporary directory. It instruments the **actual** initialization function:
a test replacement for `vkCreateDevice` returns either a failure or success
with a sentinel handle; an observation point immediately after the existing
error check stops before device dispatch or Vulkan resource allocation. Each
case runs in a fresh process. The instance and physical-device queries use the
system lavapipe ICD; the selected software device is checked in the output.
The test replacement and sentinel are not a GPU or a successful Vulkan device.

The unpatched failure case must reach the observation point with a null device;
the patched failure case must return before the point and log the injected
error. The success control must reach the point with the sentinel in both
versions. The test checks that the replacement was actually called and that
the instrumented symbol exists in the rebuilt library.

Run on the machine with the previously built fixed-version host and aemu
dependencies, compiler, Ninja, Python, `patch`, and the llvmpipe ICD already
installed. This example uses the existing local build paths:

```sh
python3 tools/diagnostics/gfxstream-createdevice-result/run.py \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/build-host \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/sysroot/usr/bin/ninja \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/prefix/lib:/var/tmp/rme-gfxstream-build.hTnuwsWV/sysroot/usr/lib/x86_64-linux-gnu \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/sysroot/usr/include
```

The copied build retains references to its original toolchain and dependencies;
these paths are not supplied by this repository. The script verifies the
examined source file SHA-256, forces the lavapipe ICD, checks the selected
device, and refuses a missing instrumented symbol. The patch is applied with
zero fuzz to a fresh source copy, which is then instrumented. No Guest, device, network, hardware node,
or product asset is modified.

The pre-existing initialization failure path may leave partial global state;
retry and cleanup after such a failure are outside this patch and were not
validated. The test stops before real device dispatch, so it does not establish
that the full host backend initializes or that Guest Vulkan or Geekbench works.
