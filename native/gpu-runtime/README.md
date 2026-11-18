# Android ARM64 GPU bundle

The `android` directory contains the Vulkan renderer and Guest client plus the
paired native OpenCL service and Guest client used in RedMagicEMU 0.6.2-beta.
The normal APK build uses this bundle when no locally generated bundle has been
staged. Set `-PrmeGpuBridgeDir=native/gpu-runtime/android` to select it explicitly.

The [Vulkan build](../vulkanbridge/client/EMU_PIPE.md#normal-apk-with-the-gpu-bridge)
and [OpenCL build](../openclbridge/README.md#regular-android-apk) describe the
source and staging process. Upstream licenses and notices are packaged under
`android/assets/rme-gpu/licenses`. The Guest index records the SHA-256 and size
of each Guest library. See the [progress log](../../docs/PROGRESS.md) for the
Adreno 750 validation.
