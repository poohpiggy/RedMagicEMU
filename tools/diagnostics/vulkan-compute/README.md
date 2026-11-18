# Standalone Vulkan compute probe

This is a computer-side diagnostic, **not an EMU Vulkan implementation**. It
does not modify or load EMU, Android firmware, or any Guest application. It
uses the native Vulkan loader, creates an offscreen Vulkan 1.2 compute pipeline,
and executes a single SPIR-V shader on 64 unsigned 32-bit words. Every word is
checked exactly against `3 * index + 7`; there is no floating-point tolerance.
Any mismatch, unsupported Vulkan 1.2, missing compute queue or Vulkan error is
a nonzero exit.

The probe prints the loader API version, each enumerated device, the selected
device type and API version, raw vendor/device/driver identifiers, queue and
host-visible memory properties. Integrated/discrete device types are preferred
over virtual and CPU devices. These types and names are **driver-reported**;
they are not independent proof of hardware acceleration. No GPU vendor or model
is hardcoded. Noncoherent host-visible memory uses explicit flush/invalidate.

The compute command is guarded by a fence with a 10-second Vulkan timeout. If
submission fails or completion cannot be confirmed, the process returns failure
without destroying potentially in-flight Vulkan resources; its OS process
teardown reclaims them. A nonconforming or hung driver might still not return
from a Vulkan call, so this is not a guarantee against every driver hang.

## Build and run on this Windows/WSL setup

Requires Python 3 on Windows, WSL with a C compiler, `libvulkan.so.1` and Mesa
lavapipe ICD at `/usr/share/vulkan/icd.d/lvp_icd.json`, plus Windows Android
NDK 28.2.13676358. No packages are downloaded and no permissions are changed.
From the repository root in PowerShell:

```powershell
python tools/diagnostics/vulkan-compute/run.py --ndk 'C:/Users/<user>/.redmagicemu-tools/android-sdk/ndk/28.2.13676358'
```

The runner compiles `compute.comp` with the NDK's `glslc`, validates the
resulting Vulkan 1.2 SPIR-V with `spirv-val`, embeds those same bytes in both
builds, and performs:

1. An Android ARM64/API 29 cross-build with 16 KiB ELF load alignment; it prints
   the binary SHA-256 but does not run it on Android.
2. Local output-check and device-preference tests.
3. An injected fence timeout path that first waits for the actual CPU test
   submission, then simulates `VK_TIMEOUT`; it must return failure without
   reporting success. This does not simulate a real GPU or driver hang.
4. An intentionally incorrect expected value, which must return failure.
5. A WSL run forced to use lavapipe, which must report `CPU software` and
   `VULKAN_COMPUTE_OK words=64`.

The temporary build is removed. To retain the cross-built executable for a
later separately authorized device test, supply an **absent** output path:

```powershell
python tools/diagnostics/vulkan-compute/run.py --ndk 'C:/Users/<user>/.redmagicemu-tools/android-sdk/ndk/28.2.13676358' --android-output "$env:TEMP/rme-vulkan-compute-arm64"
```

The output is created exclusively and never overwrites an existing file. Keep
compiled binaries out of Git. Android ARM64 was cross-compiled only: loader
availability, shell/application namespace differences, device capabilities,
GPU execution and Android 16 behavior remain untested. WSL lavapipe is CPU
software; its success does not prove Adreno support or GPU acceleration.

This one shader does not establish general Vulkan 1.2 conformance, OpenCL,
Vulkan window rendering, EMU forwarding, synchronization across processes,
or Geekbench 6 compatibility. Those each require separate implementation and
tests.
