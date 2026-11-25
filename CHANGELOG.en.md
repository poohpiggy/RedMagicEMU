# Changelog

[简体中文](CHANGELOG.md) · [繁體中文](CHANGELOG.zh-TW.md) · **English**

## RedMagicEMU 0.7.2-beta

- Enabled RedMagicOS's native virtual game-key path on the target NX809J Guest, added a **Game Switch** toggle to the top-right floating menu, and unlocked both the Control Center Game Space tile and the in-Game Space exit button. Verified entering and exiting Game Space via the floating menu on the K80.
- Fixed Vulkan client swapchain and descriptor-update compatibility: advertised and accepted `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` on readback swapchains, unwrapped Mesa `VkBuffer` handles in `vkUpdateDescriptorSetWithTemplate` on Android, and sanitized ignored descriptor pointers/handles in `vkUpdateDescriptorSets` and template updates. Verified RedMagic Mora and its AR camera mode open without crashing from Game Space on the K80.
- Fixed NX809J Guest boot crashes (`zygote code 137`) on hosts whose SELinux/DAC policy blocks ordinary apps from listing `/sys/class/thermal/` (such as some MediaTek devices), where `CubeService` (`nbopt:data_center`) threw an uncaught `ExceptionInInitializerError` when `listFiles()` returned `null`: Guest now gets an app-readable empty `/sys/class/thermal` directory instead of falling through to the host's thermal nodes.
- Expanded the `zygote code 137` crash summary limit from 240 to 1024 characters so host logs and `crashes.log` preserve full `Caused by` stack traces.

[0.7.2-beta release notes](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.7.2-beta)

## RedMagicEMU 0.7.1-beta

- Fixed firmware imports that failed with `Invalid instance directory` when the Android host used a path alias for the app data directory.
- Firmware import checks the app data directory and `files/images` before extraction and identifies the affected directory if a check fails.
- Automatically hides Guest Vulkan capability declarations when the host Vulkan renderer fails to start or GPU library preparation rolls back, leaving GLES and OpenCL unaffected.
- Surfaces the `system_server` crash summary directly in the host log and failure message when `zygote` exits with `code 137`, or points to host memory and Android child-process limits when no crash marker is present.
- Expands non-root Guest log copy and export to include `crashes.log` alongside the `crash`, `system`, and `main` log buffers.

[0.7.1-beta release notes](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.7.1-beta)

## RedMagicEMU 0.7.0-beta

- Added Magisk Root to instance settings. It is off by default. Enabling it and restarting the Guest activates the bundled Magisk for RedMagicEMU 31.1. Each instance retains its configuration and preinit data. On the K80, tested granting and revoking root access for regular apps and booting the Guest after disabling Root.
- The sensor toggle now controls location and orientation data. The Guest receives host location updates, live GNSS results, details for the current serving LTE cell, and uncalibrated gyroscope and magnetometer readings on demand. Amap received location updates and entered follow mode on the K80.
- The AVC and HEVC hardware decoding bridge supports resolution changes during playback. The K80 passed 720p → 1080p → 720p switches through the bridge, and Bilibili continued playing after a quality change.
- The camera selects HD streams based on host capabilities. It can crop 1920×1080 and 1280×720 widescreen streams to a 4:3 preview at native pixel resolution. Digital zoom uses a center crop.
- Fixed the orientation of photos and their watermarks, as well as the preview and recorded output at 1080p60. Standard video is cropped to the output aspect ratio. The K80 produced correctly oriented previews and recordings at 720p30 and 1080p30.
- Disabling the Vulkan bridge and restarting the Guest removes the Vulkan capability declarations while keeping GLES available. Enabling the bridge restores those declarations.
- Corrected the cached memory type for shared Adreno buffers. The K80 scored 16,178 in Geekbench 6 Vulkan and passed window presentation regression tests.
- Carried forward the location, power, and Vulkan improvements from 0.6.9-beta and removed legacy Guest boot property overrides.
- The matching NX809J 11.0.21mr firmware comes in 7 download parts with instructions for combining and verifying them. The base firmware carries the credit **by 酷安@某贼 2025.6.29**. RedMagicEMU adapted it for Guest import. [Firmware download](https://github.com/poohpiggy/RedMagicEMU-NX809J-VM/releases/tag/11.0.21mr)

[0.7.0-beta release notes](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.7.0-beta)

## RedMagicEMU 0.6.9-beta (unreleased)

- Location subscriptions start on demand. A GPS request from a foreground Guest app starts host location updates. The listener is released when the app exits or the host backgrounds RedMagicEMU. Amap received 11 location updates on the K80.
- Automatically retries starting the battery service once if it times out during boot. Both a cold boot and a Guest restart reached the system in about 41 seconds on the K80. Cellular data retries stop on virtual Wi-Fi and Ethernet. In LTE mode, Android handles retry backoff.
- Reused swapchain readback commands, moved GPU waits and readbacks to a separate thread, and streamlined host Vulkan command decoding. The combined test build completed AnTuTu 12.0.3 on the K80 with an overall score of 1,507,188 and a GPU score of 233,389.

## RedMagicEMU 0.6.8-beta (unreleased)

### Graphics Bridge and Hardware Encoding

- Identified the file descriptors carrying shared buffer data from host mappings and consolidated DMA-BUF requests.
- Added host AVC hardware encoding. The K80 completed AnTuTu's 1080p30 transcode. A consistent NV12 layout fixed green and purple tint in the output.

### Transcode Completion and Build Comparison

- Fixed a media service Binder crash at the end of asynchronous transcoding. A separate K80 test transcoded all 462 frames on three consecutive runs.
- Retained the bidirectional Vulkan pipe. In two full K80 runs, the unidirectional build scored 1,397,041 overall and 125,613 on the GPU. The bidirectional build scored 1,414,315 overall and 157,925 on the GPU.

## RedMagicEMU 0.6.7-beta (unreleased)

- Fixed rear camera digital zoom and a crash when switching to video mode. JPEG capture uses the sizes advertised by the host camera. On the K80, the rear camera captured at 4096×3072 and the front camera at 2592×1944.
- Enabled rear camera recording at 1080p60 in standard and Pro modes and corrected video and audio timestamps. Pro mode supports ISO, shutter speed, manual focus, EV, and white balance temperature and tint.
- Compatible rear cameras can save a DNG and a JPEG from the same exposure. Both formats were captured and decoded on the K80, and standard video recording averaged 60.10 fps.

## RedMagicEMU 0.6.6-beta

- Fixed a crash in the stock camera's GLES external texture preview.
- Added Vulkan support for Android windows and swapchains so drawing commands reach the host rendering thread directly. The K80 completed the GPU scenes in standard AnTuTu.
- Added H.264 and HEVC hardware decoding on Android 16 hosts with compatible decoders and fixed a media Binder file descriptor leak.
- Added import progress to the home screen, instance settings, and data reset. The floating menu gained a keep-screen-on option.

[0.6.6-beta release notes](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.6-beta)

## RedMagicEMU 0.6.5-beta (unreleased)

- The instance card on the home screen displays import progress. Large ZIP files can be read directly from host storage. Extraction and verification run in parallel, with checks for archive entries, file sizes, CRCs, partition structure, and firmware fingerprints.
- Importing the 12.41 GiB firmware took about 75 seconds on the K80. The first boot took about 103 seconds after import.

## RedMagicEMU 0.6.4-beta (unreleased)

- Redesigned the home and About screens. The home screen brings the instance, target firmware, and common actions together.
- Added five instance memory options at 4, 6, 8, 12, and 16 GB. Navigation buttons and vertical sync are configured per instance, and the saved values apply on the next boot.

## RedMagicEMU 0.6.3-beta (unreleased)

- Added Guest data reset to instance settings. It preserves firmware, instance options, and shared files, and is available when the instance is stopped.
- Adapted the stock `/cache` relative symlink. After a reset, an isolated K80 instance returned to the stock welcome screen.

## RedMagicEMU 0.6.2-beta

- Standard APK builds automatically use host Vulkan and native OpenCL. Support includes FP16, asynchronous linear buffer readback, and nonblocking uploads.
- The K80's Adreno 750 completed Geekbench 6 Vulkan and OpenCL tests. The performance overlay includes memory used by the host process and driver.

[0.6.2-beta release notes](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.2-beta)

## RedMagicEMU 0.6.1-beta (unreleased)

- Investigated the AnTuTu 3D Lite Unity crash and fixed GLES pixel storage, buffer mapping, and repeated binding issues. An isolated K80 test build completed two Unity scenes.
- Built the host Vulkan service, Guest protocol, and compute path. Window, swapchain, and presentation protocols entered offline testing and were integrated into the runtime in a later version.

## RedMagicEMU 0.6.0-beta

- Added battery sync to instance settings and the floating menu. Charge level and charging status can follow the host or be set manually.
- Extended the accelerometer bridge to include the gyroscope, magnetometer, and host location. Corrected sensor flags and the location entry point to restore the stock compass.
- Enabled data on virtual LTE and fixed connectivity that persisted after virtual Wi-Fi was turned off, along with related boot crashes. Restored front and rear cameras and standard photo capture.
- Consolidated builds into the `app.redmagicemu` package with US English, UK English, Simplified Chinese, Traditional Chinese (Taiwan), and Traditional Chinese (Hong Kong). Other system languages fall back to US English. New instances default to a 75% layout.

[0.6.0-beta release notes](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.0-beta)

## RedMagicEMU 0.5.0-beta (unreleased)

### First Pass

- Streamlined the engine, import path, and feature dependencies for the specified NX809J firmware and removed obsolete prototypes.

### Second Pass

- Kept instance data reset and the paired graphics resources, and removed a legacy instance deletion path with no entry point. Selected a build of about 41.6 MiB.

## RedMagicEMU 0.4.x (unreleased)

### RedMagicEMU 0.4.8-beta (unreleased)

- Tested the stock camera preview, front and rear camera switching, browser camera access, photo capture, and saving to DCIM. Version 0.6.6-beta fixed the preview crash in release builds, and 0.6.7-beta expanded native resolution capture and video recording.

### RedMagicEMU 0.4.7-beta (unreleased)

- Quick shutdown now cancels system ringtone indexing in progress.
- Added fixed Guest refresh rate options of 60, 90, 120, and 144 Hz and verified switching among all four.

### RedMagicEMU 0.4.6-beta (unreleased)

- The first pass mapped how the stock Sensors multi-HAL loads and established host sample timing and the Guest event interface.
- The second pass enabled the three-axis accelerometer. The Guest system and regular apps received readings that changed as the phone moved.

### RedMagicEMU 0.4.5-beta (unreleased)

- Removed the capacity slider and preserved the storage configuration of existing instances.

### RedMagicEMU 0.4.4-beta (unreleased)

- The first pass prototyped an open hotspot, status notifications, and Ethernet handoff.
- The second pass synchronized WLAN settings, Quick Settings, and the status bar and fixed DNS and startup ordering. Exiting airplane mode restores the previous Wi-Fi toggle state. Cleaned up the floating menu entry.

### RedMagicEMU 0.4.3-beta (unreleased)

- Added LTE signal reporting to the emulated modem. The Guest status bar shows signal bars and restores them after cycling airplane mode.

### RedMagicEMU 0.4.2-beta (unreleased)

- New instances automatically prepare compatibility data for the always-on display with the specified Wuthering Waves theme. Tested foreground character placement, the wake animation, and restoration after switching themes.

### RedMagicEMU 0.4.1-beta (unreleased)

- Adapted NX809J `ztecfg` to restore the stock wallpapers and icons for the specified Wuthering Waves theme.
- Removed the legacy modem entry point. Each instance generates a virtual IMEI, and the system interface displays the virtual IMEI SV.

## RedMagicEMU 0.3.0-beta

- **1A** Started from the stock launcher boot baseline and checked display, touch, text input, shared storage, and APK installation.
- **1B** Fixed text input, enabled simulated SMS and incoming calls, and fixed a second incoming call failing after the first was answered and ended.
- **1C** Passed Ethernet and host audio playback regression tests and added stock ringtone indexing and default ringtone settings.
- **1D** Extended regression coverage for recording, media, and apps and fixed the standard vibration bridge. This version covered consecutive incoming calls, timer vibration, web browsing, and audio and video playback.

[0.3.0-beta release notes](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.3.0-beta)

## RedMagicEMU 0.2.0-alpha

- Fixed the charging service starting during a normal boot and addressed a telephony service dependency in the target firmware. The Guest reached the stock launcher for the first time.
- Tested a fresh import, installation over an existing copy, Guest restart and shutdown, and stopping the Guest from the host menu on a physical device.

[0.2.0-alpha release notes](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.2.0-alpha)

## RedMagicEMU 0.1.0-alpha

- **1A** Established the RedMagicEMU app identity, icon, project description, and shared directory.
- **1B** Added ZIP, partition, and fingerprint checks for the specified NX809J firmware and fixed N64 engine selection for ARM64 instances.
- **1C** Simplified the home screen to one instance and added import cancellation, staging cleanup, and notices for interrupted imports.

[0.1.0-alpha release notes](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.1.0-alpha)

## S0 · Baseline Audit

- Used AEmulator as the baseline and traced the firmware import, Guest boot, and host bridging entry points. Set up JDK 21, SDK 36, and NDK r28 and built the first baseline APK.

## AEmulator 0.0.0.3 (upstream)

[AEmulator 0.0.0.3 upstream changelog](CHANGELOG.md#aemulator-0003-upstream)
