<p align="center">
  <img src="assets/redmagicemu-icon.svg" width="128" height="128" alt="RedMagicEMU">
</p>

<h1 align="center">RedMagicEMU</h1>

**简体中文** · [繁體中文](README.zh-TW.md) · [English](README.en.md)

RedMagicEMU 是一个独立维护的 Android 固件模拟器。当前支持导入 REDMAGIC NX809J 的原厂 Android 16（API 36、ARM64）固件，在 Android 宿主上运行原厂系统。宿主设备品牌不限。

## 已实现的功能

- **固件导入**。导入原厂 ZIP，检查归档条目、镜像大小、CRC、分区结构和固件指纹。
- **图形与媒体**。支持 GLES、Vulkan 窗口呈现和宿主原生 OpenCL，接入 H.264 与 HEVC 硬解。
- **实例管理**。实例独立保存，支持数据重置、内存设置、屏幕比例调整和 Guest 电池状态设置。
- **Guest Root**。实例设置提供默认关闭的 Magisk Root。开启后，可在 Guest 中通过 Magisk for RedMagicEMU 授予或撤销应用的 Root 权限。
- **相机**。支持前后摄切换和宿主原生 JPEG 尺寸。兼容的后摄可同时保存 RAW/DNG 和 JPEG。后摄 1080p60 录像已在 Redmi K80 上验证。
- **设备功能**。支持音频播放和录音、振动、加速度计、陀螺仪、磁力计，并可按需同步宿主位置。支持指定原厂主题的息屏显示。
- **游戏空间与红魔姬**。支持虚拟竞技键（浮动菜单开关与控制中心「游戏空间」磁贴），可顺畅进出游戏空间；游戏空间内的红魔姬及 AR 相机模式可用。
- **网络与通信**。支持 Ethernet、虚拟 Wi-Fi、虚拟 LTE、模拟来电和短信。
- **操作与文件**。支持宿主与 Guest 共享文件、安装 APK、Android 导航键、浮动菜单、截图和常亮。
- **界面语言**。支持美式英语、英式英语、简体中文、繁体中文（台湾）和繁体中文（香港）。

## 安装与构建

下载安装 APK，自行准备对应的原厂固件 ZIP，然后从首页导入并启动实例。

源码构建需要 JDK 21、Android SDK 36 和 NDK r28。

```sh
./gradlew :app:assembleRelease
```

构建包含 Magisk Root 的版本时，请使用 [Magisk 适配版构建说明](third_party/magisk/README.md) 中的 APK 和校验参数。

## 致谢与许可证

RedMagicEMU 是非官方项目，基于 [AEmulator](https://github.com/uxazu/AEmulator) 和 [SENSES](https://github.com/Istratii-tech/SENSES) 的工作。

项目按 [GPL-3.0](LICENSE) 发布。商标和原厂固件归各自所有者所有。
