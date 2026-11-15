# NX809J primary emulated volume

The Android 16 NX809J framework recognizes the emulated primary volume for user
0 only when its ID is `emulated;0`. The target-only VoldStub JAR announces that
ID on reset and user start; the previous `/data/media` and `/storage/emulated`
paths and mount ordering are retained. Other guests use the existing shared JAR.
This does not change or claim any storage capacity or quota.

Rebuild with `python native/gueststubs/build_target_vold.py --sdk <Android SDK>
--output app/src/main/assets/engines/redmagic/vold-service.jar`, then rebuild
the APK so its engine asset index is refreshed. TreeFixer checks ownership and
updates the runtime JAR; launch selects it ahead of the shared JAR only after a
successful preparation. On preparation failure the original service is used.

To roll back, restore the prior VoldStub launch and source changes, remove the
target asset from the APK build, and rebuild. The target-owned runtime JAR can
remain unused; removing it is optional and must be limited to the owned file.
No original firmware needs to be modified. Guest mount and camera behavior still
require on-device verification.

On the target instance, a diagnostic APK containing this JAR reported volume
`emulated;0` as mounted, primary, emulated and not removable; before the change
it reported `emulated` as removable. The stock camera's storage-capacity log
changed from -3 to a positive value. A later still capture reached the JPEG
callback but the stock app crashed on missing exposure-result metadata; that
does not establish a saved or decodable photo.
