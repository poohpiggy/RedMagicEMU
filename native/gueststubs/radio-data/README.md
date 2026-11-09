# Isolated Android 16 Radio data prototype

This unregistered `slot1` test service creates no cellular data bearer and does not access host networking or SIM information. An explicitly configured synthetic radio that is ON reports an empty data-call list; an OFF or disconnected radio reports `RADIO_NOT_AVAILABLE`. Data-call setup and the other unimplemented operations respond with `REQUEST_NOT_SUPPORTED` and the original serial. This is not an Internet implementation or evidence that Android telephony initialization is complete.

`aidl/data` contains the frozen Android Open Source Project Radio data AIDL v4 snapshot from `platform/hardware/interfaces`, tag `android-16.0.0_r1`, path `radio/aidl/aidl_api/android.hardware.radio.data/4`. The original notices remain in the source files; the Apache License 2.0 copy is in `../radio/aidl/LICENSE-2.0.txt`. The `.hash` is the official interface hash, not a checksum of this directory. Builds never fetch or update upstream.

Run `python native/gueststubs/radio/verify_offline.py --sdk <local-android-sdk>` for the isolated host contract tests and test DEX. Generated `android.*` interfaces are compile-only and are not packaged. Real Binder serialization, process death/rebind races, lifecycle safety, and Guest framework interaction have not been validated; this prototype must not be registered as a production service.
