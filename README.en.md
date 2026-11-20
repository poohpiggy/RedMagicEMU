<p align="center">
  <img src="assets/redmagicemu-icon.svg" width="128" height="128" alt="RedMagicEMU">
</p>

<h1 align="center">RedMagicEMU</h1>

[简体中文](README.md) · [繁體中文](README.zh-TW.md) · **English**

RedMagicEMU is an independently maintained Android firmware emulator. It currently imports stock REDMAGIC NX809J firmware based on Android 16 (API 36, ARM64) and runs the stock system on an Android host. The host can be from any device brand.

[Download the 0.6.6-beta APK](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.6-beta)

## 0.6.6-beta

- The home screen uses instance cards to show firmware and runtime status. Cards show import progress, and optimized extraction and verification reduce the wait.
- You can reset data for individual instances. Instance settings include memory, resolution, and vertical sync options. The floating menu can keep the screen on.
- Vulkan supports Android windows and swapchains. This release fixes a crash on launch in an AnTuTu GPU scene and reduces command round trips and frame copying between the host and guest.
- On Android 16 hosts with the corresponding hardware decoders, the guest can use the host to decode H.264 and HEVC. This release also fixes a Binder descriptor leak when the media service releases descriptors.

## Features

- **Firmware import.** Imports the stock ZIP and checks archive entries, image sizes, CRCs, partition structure, and firmware fingerprints.
- **Graphics and media.** Supports GLES, Vulkan window presentation, native host OpenCL, and hardware decoding for H.264 and HEVC.
- **Instance management.** Instances are stored separately. Each supports data reset, memory settings, aspect-ratio adjustment, and guest battery settings.
- **Device features.** Supports audio playback and recording, front and rear camera switching, standard photo capture, vibration, accelerometer, gyroscope, magnetometer, and host location. The always-on display works with the specified stock theme.
- **Networking and telephony.** Supports Ethernet, virtual Wi-Fi, virtual LTE, simulated incoming calls, and SMS.
- **Controls and files.** Supports file sharing between host and guest, APK installation, Android navigation buttons, a floating menu, screenshots, and a keep-screen-on option.
- **Interface languages.** Supports US English, UK English, Simplified Chinese, Traditional Chinese (Taiwan), and Traditional Chinese (Hong Kong).

## Installation and build

Download and install the APK, obtain the matching stock firmware ZIP, then import and start the instance from the home screen.

Building from source requires JDK 21, Android SDK 36, and NDK r28.

```sh
./gradlew :app:assembleRelease
```

## Credits and license

RedMagicEMU is an unofficial project based on the work of [AEmulator](https://github.com/uxazu/AEmulator) and [SENSES](https://github.com/Istratii-tech/SENSES).

The project is released under the [GPL-3.0](LICENSE). Trademarks and stock firmware belong to their respective owners.
