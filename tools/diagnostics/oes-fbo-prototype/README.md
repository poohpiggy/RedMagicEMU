# Temporary OES framebuffer experiment

This is a diagnostic, not a production fix. `build.py` copies the current
graphics bridge into a new system temporary directory, applies the experiment
only to that copy, and builds two ARM64 libraries there. It never modifies or
installs the APK, a Guest image, or the production graphics sources.

The experimental guest opts in only `com.android.camera` and the
`rme_glfbo_` probe process when an OES EGLImage references an RGBA AHB with
usage `0x33`. An experimental bit in the existing image-bind target identifies
that request to the matching host. The host associates the texture and AHB,
attaches the host 2D texture to the FBO, and the guest asks the existing
gralloc path to read the buffer back. Both libraries must be used together.

The old readback protocol cannot distinguish no-update from failure; it may
lose a frame on disconnect, has no durable frame identity across allocator
restarts, and does not safely handle every concurrent write or lifetime. The
host's FBO map records a buffer ID and may become stale if the attached texture
is rebound without a subsequent framebuffer attachment. The stock camera JNI
also does not propagate buffer lock failures. These limitations rule out
shipping this prototype or treating a passing probe as complete camera support.

From PowerShell, using an installed NDK:

```powershell
python tools/diagnostics/oes-fbo-prototype/build.py --ndk C:/Users/<user>/.redmagicemu-tools/android-sdk/ndk/28.2.13676358
```

The command prints the temporary source directory, both library paths, and
their SHA-256 hashes. To exercise the generated host functions and the guest
gate on a Linux host or WSL, replace `<source>` with the printed source directory:

```sh
python3 tools/diagnostics/oes-fbo-prototype/test_attachment.py <source>/host.c
python3 tools/diagnostics/oes-fbo-prototype/test_image_bind.py <source>/host.c
python3 tools/diagnostics/oes-fbo-prototype/test_guest_gate.py <source>/guest.c
```

The native probe in `tools/diagnostics/ahb-gl-fbo-native` checks two changing
frames, GPU and CPU pixels, repeated `lockPlanes`, OES sampling, and two buffer
lifetimes. It must be run against a Guest actually using both matching diagnostic
libraries. Confirm the process name and GPU bridge in the logs. The probe does
not open the real camera, exercise padded rows, or establish fault recovery.

On 2026-11-15, the matching diagnostic libraries were packaged in the standard
debug APK and installed over the existing package with the user's authorization.
The probe passed both OES buffer lifetimes and both changing test frames on the
target GPU. The user reported that the stock camera's front and rear preview
both work; stock still capture showed a storage-preparation message and has not
been validated. The installed APK is experimental, with the limitations above.
The previous installed debug APK was backed up at
`%TEMP%/rme-oes-fbo-DIAGNOSTIC-351x95pw/original-standard-debug.apk`
(SHA-256 `474adee3436d7ce33a91671dbf6a93cebe0616cb278e28360e30db3af0570209`).
To roll back, stop the guest normally, install that APK over the same package,
and restart the same guest so its engine asset is refreshed. Keep the instance
and original firmware. Temporary OS files can be cleared independently, so
copy the backup elsewhere before relying on it long term.

Later on the same date, the installed diagnostic APK was updated to
`camera-lux-diagnostic.apk` in the same temporary directory (SHA-256
`cc986cd19d6a7dc89d323e943533fada7b01cd2a7ed67d71fa35dc300709f132`). It
also contains the target-specific Vold service and a temporary HAL with nominal
virtual exposure/ISO results and a registered optional lux key. The stock app
reported successful storage for one still capture, and the user confirmed a
photo opens in the stock gallery. This does not resolve the graphics protocol
limitations above. The previously installed preview-and-storage test APK is
`camera-vold-diagnostic.apk` in the same directory. All these temporary files
may be removed by the operating system.
