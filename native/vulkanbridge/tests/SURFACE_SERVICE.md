# Private Vulkan Surface Binder candidate

`VulkanSurfaceService` is declared `exported=false` in the app's dedicated
`:vulkan` process. Its AIDL interface sends Android's `Surface` Parcelable; no
native pointer is sent over Binder. The service is not started or bound by any
app UI. Its renderer is not implemented or loaded, so status, attach and
detach always return `NOT_READY` (`0`). A received remote Surface copy is
released when rejected; a local caller's own object is not released. The AIDL
default implementation also returns zero, so it cannot accidentally report
renderer readiness.

This only provides the private transport endpoint. It does not yet retain a
Surface, connect JNI, start a renderer, or synchronize swapchain teardown.
No code here treats `onUnbind` or `stopSelf` as proof that the process has
exited; a future restart coordinator must observe process death before restart.

The following offline test runs with a cached Android SDK and Gradle setup:

```powershell
& .\gradlew.bat '-Dorg.gradle.java.home=C:\Program Files\Java\jdk-21.0.10' `
    :app:testDebugUnitTest --tests app.aemu.core.VulkanSurfaceAdmissionTest --offline
```

This compiles the AIDL and service, and runs the ownership tests with
substitutes. It does not run Android Binder, launch the service, build an APK,
or access a device. Test `Surface` transport and process death on a device
only after renderer and teardown wiring are complete.
