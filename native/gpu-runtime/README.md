# Android ARM64 GPU bundle

The `android` directory contains the paired Vulkan renderer and Guest client
and the paired native OpenCL service and Guest client. It retains the
asynchronous Vulkan readback and optimized host decoder from 0.6.9.
For the verified Adreno 750 driver, the existing coherent Android hardware BLOB
mapping also reports CPU cacheability. The Guest and host driver access the
same backing memory.
The normal APK build uses this bundle when no locally generated bundle has been
staged. Set `-PrmeGpuBridgeDir=native/gpu-runtime/android` to select it explicitly.

The [Vulkan build](../vulkanbridge/client/EMU_PIPE.md#normal-apk-with-the-gpu-bridge)
and [OpenCL build](../openclbridge/README.md#regular-android-apk) describe the
source and staging process. Upstream licenses and notices are packaged under
`android/assets/rme-gpu/licenses`. The Guest index records the SHA-256 and size
of each Guest library. The renderer exchanges commands directly with authenticated
Guest Vulkan clients.
