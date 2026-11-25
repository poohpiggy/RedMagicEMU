<p align="center">
  <img src="assets/redmagicemu-icon.svg" width="128" height="128" alt="RedMagicEMU">
</p>

<h1 align="center">RedMagicEMU</h1>

[简体中文](README.md) · [繁體中文](README.zh-TW.md) · **English**

RedMagicEMU is an independently maintained Android firmware emulator. It currently imports stock REDMAGIC NX809J firmware based on Android 16 (API 36, ARM64) and runs the stock system on an Android host. The host can be from any device brand.

## Features

- **Firmware import.** Imports the stock ZIP and checks archive entries, image sizes, CRCs, partition structure, and firmware fingerprints.
- **Graphics and media.** Supports GLES, Vulkan window presentation, native host OpenCL, and hardware decoding for H.264 and HEVC.
- **Instance management.** Instances are stored separately. Each supports data reset, memory settings, aspect-ratio adjustment, and guest battery settings.
- **Guest Root.** Magisk Root is off by default in instance settings. Once enabled, you can grant or revoke app root access in the Guest through Magisk for RedMagicEMU.
- **Camera.** Supports switching between front and rear cameras and capturing JPEGs at native host resolutions. Compatible rear cameras can save paired RAW/DNG and JPEG captures. Rear 1080p60 recording has been tested on the Redmi K80.
- **Device features.** Supports audio playback and recording, vibration, accelerometer, gyroscope, magnetometer, and on-demand host location updates. The always-on display works with the specified stock theme.
- **Game Space and RedMagic Mora.** Supports the virtual Game Switch (floating menu toggle and Control Center Game Space tile) to enter and exit Game Space cleanly; RedMagic Mora and its AR camera mode work inside Game Space.
- **Networking and telephony.** Supports Ethernet, virtual Wi-Fi, virtual LTE, simulated incoming calls, and SMS.
- **Controls and files.** Supports file sharing between host and guest, APK installation, Android navigation buttons, a floating menu, screenshots, and a keep-screen-on option.
- **Interface languages.** Supports US English, UK English, Simplified Chinese, Traditional Chinese (Taiwan), and Traditional Chinese (Hong Kong).

## Installation and build

Download and install the APK, obtain the matching stock firmware ZIP, then import and start the instance from the home screen.

Building from source requires JDK 21, Android SDK 36, and NDK r28.

```sh
./gradlew :app:assembleRelease
```

To include Magisk Root, use the APK and verification flags in the [Magisk build instructions](third_party/magisk/README.md).

## Credits and license

RedMagicEMU is an unofficial project based on the work of [AEmulator](https://github.com/uxazu/AEmulator) and [SENSES](https://github.com/Istratii-tech/SENSES).

The project is released under the [GPL-3.0](LICENSE). Trademarks and stock firmware belong to their respective owners.
