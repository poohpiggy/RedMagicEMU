# RedMagicEMU

RedMagicEMU 是基于 [AEmulator](https://github.com/uxazu/AEmulator) 独立维护的非官方项目；AEmulator 又基于原始 [SENSES 模拟器](https://github.com/Istratii-tech/SENSES)。本项目不是红魔官方产品，也与红魔无关联。

项目以 REDMAGIC NX809J 原厂 Android 16 / API 36 / ARM64 固件作为 Guest 目标。此限制适用于 Guest 固件，不限制宿主设备型号。该固件的支持情况和运行效果尚未在设备上验证；参阅[进度](../PROGRESS.md)和[基线](../BASELINE.md)。

## 构建

Android 项目需要 Android SDK 36 和 NDK r28。基线构建使用 JDK 21：

```shell
./gradlew :app:assembleStandardDebug
```

本地开发使用应用 ID `app.redmagicemu`。Android 将其视为独立于上游应用的安装，私有数据不会自动迁移。在迁移方法经过验证之前，请保留旧应用及其数据。

## 致谢与许可证

RedMagicEMU 基于 [uxazu](https://github.com/uxazu) 的 AEmulator；后者源于[原作者](https://t.me/istratii_tech1)的 HTC Desire HD 和 HTC One M7 模拟器。我们保留并感谢他们的贡献及现有版权和许可证声明。

本项目按 [GPL-3.0](../../LICENSE) 发布。Android、REDMAGIC、其他商标及固件归各自所有者所有。
