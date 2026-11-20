<p align="center">
  <img src="assets/redmagicemu-icon.svg" width="128" height="128" alt="RedMagicEMU">
</p>

<h1 align="center">RedMagicEMU</h1>

RedMagicEMU 是一个独立维护的 Android 固件模拟器。当前支持导入 REDMAGIC NX809J 的原厂 Android 16（API 36、ARM64）固件，在 Android 宿主上运行原厂系统。宿主设备品牌不限。

[下载 0.6.6-beta APK](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.6-beta)

## 0.6.6-beta

- 首页以实例卡片展示固件和运行状态。导入时在卡片上显示进度，缩短了解包和校验的等待时间。
- 可以独立重置实例数据。实例设置提供内存、分辨率和垂直同步选项，浮动菜单可以保持屏幕常亮。
- Vulkan 支持 Android 窗口和交换链。修复安兔兔 GPU 场景启动闪退，并减少宿主与 Guest 之间的命令往返和画面拷贝开销。
- Android 16 宿主支持相应硬件解码器时，Guest 可调用宿主解码 H.264 和 HEVC。修复媒体服务释放 Binder 描述符时的泄漏。

## 已实现的功能

- **固件导入**。导入原厂 ZIP，检查归档条目、镜像大小、CRC、分区结构和固件指纹。
- **图形与媒体**。支持 GLES、Vulkan 窗口呈现和宿主原生 OpenCL，接入 H.264 与 HEVC 硬解。
- **实例管理**。实例独立保存，支持数据重置、内存设置、屏幕比例调整和 Guest 电池状态设置。
- **设备功能**。支持音频播放和录音、前后摄切换与普通拍照、振动、加速度计、陀螺仪、磁力计和宿主位置。支持指定原厂主题的息屏显示。
- **网络与通信**。支持 Ethernet、虚拟 Wi-Fi、虚拟 LTE、模拟来电和短信。
- **操作与文件**。支持宿主与 Guest 共享文件、安装 APK、Android 导航键、浮动菜单、截图和常亮。
- **界面语言**。支持美式英语、英式英语、简体中文、繁体中文（台湾）和繁体中文（香港）。

## 安装与构建

下载安装 APK，自行准备对应的原厂固件 ZIP，然后从首页导入并启动实例。

源码构建需要 JDK 21、Android SDK 36 和 NDK r28。

```sh
./gradlew :app:assembleRelease
```

## 致谢与许可证

RedMagicEMU 是非官方项目，基于 [AEmulator](https://github.com/uxazu/AEmulator) 和 [SENSES](https://github.com/Istratii-tech/SENSES) 的工作。

项目按 [GPL-3.0](LICENSE) 发布。商标和原厂固件归各自所有者所有。
