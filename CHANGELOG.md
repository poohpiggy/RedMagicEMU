# 更新日志

**简体中文** · [繁體中文](CHANGELOG.zh-TW.md) · [English](CHANGELOG.en.md)

## RedMagicEMU 0.7.2-beta (unreleased)

- 为目标 NX809J Guest 启用原生虚拟竞技键通道，在右上角浮动菜单新增「竞技键」开关，并同步解锁控制中心「游戏空间」磁贴与游戏空间内原生退出按钮。K80 实测通过浮动菜单顺畅退出与重新进入游戏空间。
- 修复 Vulkan 客户端交换链与描述符更新兼容问题：补充 `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` 交换链用途支持，修复 `vkUpdateDescriptorSetWithTemplate` 下 Mesa `VkBuffer` 解包，并清理描述符更新中按规范应忽略的指针与句柄。K80 实测游戏空间内的红魔姬及 AR 相机模式可正常进入且不再闪退。

## RedMagicEMU 0.7.1-beta

- 修复部分宿主手机导入固件时报 `Invalid instance directory` 的问题，兼容宿主 Android 为应用数据目录提供的路径别名。
- 导入前检查应用数据目录和 `files/images`，发现异常时指出具体目录。
- 当宿主 Vulkan 渲染器启动失败或 GPU 库准备回滚时，自动撤下 Guest 的 Vulkan 能力声明，不影响 GLES 与 OpenCL。
- `zygote` 以 `code 137` 退出时，在宿主日志和报错信息中直接输出 `system_server` 崩溃摘要，未发现崩溃痕迹时提示排查宿主内存或 Android 子进程限制。
- 增强无 Root 场景下的 Guest 日志复制与导出，合并输出 `crashes.log` 与 `crash`、`system`、`main` 日志缓冲。

[0.7.1-beta 发布说明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.7.1-beta)

## RedMagicEMU 0.7.0-beta

- 实例设置新增默认关闭的 Magisk Root。开启后重启 Guest 即可使用内置的 Magisk for RedMagicEMU 31.1，配置和 preinit 数据随实例保存。K80 实测通过普通应用授权、撤销，以及关闭 Root 后的启动。
- 定位和方向数据统一由传感器开关控制。Guest 按需接收宿主定位、实时 GNSS 结果、当前 LTE 服务小区，以及未校准的陀螺仪和磁力计数据。K80 上高德收到位置更新并进入跟随模式。
- AVC 和 HEVC 硬解桥支持播放时切换分辨率。K80 分别跑通硬解桥的 720p → 1080p → 720p 切换，以及 B 站客户端切画质后续播。
- 相机按宿主能力选高清流。1920×1080 和 1280×720 的宽屏流可裁切成 4:3 取景，保留原始像素。数字变焦继续使用中心裁切。
- 修正照片内容和水印方向，并修复 1080p60 档位的取景与成片方向。常规录像按输出比例裁切，K80 上 720p30 和 1080p30 的取景、成片均恢复正常。
- 关闭 Vulkan 桥接并重启 Guest 后撤下 Vulkan 能力声明，GLES 继续可用。重新开启后恢复声明。
- 修正 Adreno 共享缓冲区的缓存内存类型。K80 完成 Geekbench 6 Vulkan 跑分，得分 16,178，并通过窗口呈现回归。
- 沿用 0.6.9 的定位、电源和 Vulkan 优化，清理旧的 Guest 启动属性覆盖。
- 配套的 NX809J 11.0.21mr 固件提供 7 个下载分卷及合并校验说明。底包署名为 **by 酷安@某贼 2025.6.29**，RedMagicEMU 负责 Guest 导入适配。[固件下载](https://github.com/poohpiggy/RedMagicEMU-NX809J-VM/releases/tag/11.0.21mr)

[0.7.0-beta 发布说明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.7.0-beta)

## RedMagicEMU 0.6.9-beta (unreleased)

- 定位由常驻订阅改为按需启停。Guest 前台应用发起 GPS 请求时向宿主取位置，退出应用或退到宿主后台后释放监听。K80 高德收到 11 次位置更新。
- 开机等待电池服务超时时自动恢复一次服务，K80 冷启动和 Guest 重启均约 41 秒进入系统。虚拟 Wi-Fi 和以太网下停止无效的蜂窝数据重试，LTE 模式交由 Android 按策略退避。
- 复用交换链回读命令，把 GPU 等待与回读放到独立线程，并优化宿主 Vulkan 命令解码。K80 的组合测试包跑完安兔兔 12.0.3，总分 1,507,188，GPU 233,389。

## RedMagicEMU 0.6.8-beta (unreleased)

### 图形桥与硬编

- 依据宿主实际映射识别共享缓冲区的数据 FD，合并 DMA-BUF 请求。
- 接入宿主 AVC 硬编，K80 跑通安兔兔 1080p30 转码。统一 NV12 布局，修复成片绿紫偏色。

### 转码收尾与候选对比

- 修复异步转码结束时媒体服务的 Binder 崩溃。K80 的独立对照连续三轮完整转码 462 帧。
- 保留双向 Vulkan 管道。K80 两轮整跑中，单向包总分 1,397,041、GPU 125,613，双向包总分 1,414,315、GPU 157,925。

## RedMagicEMU 0.6.7-beta (unreleased)

- 修复后摄数字变焦和切换录像模式闪退。按宿主镜头公开的 JPEG 尺寸拍照，K80 后摄 4096×3072、前摄 2592×1944。
- 打通后摄普通和专业 1080p60 录像，校正画面与录音时间戳。专业模式接通 ISO、快门、手动对焦、EV、白平衡色温和色调。
- 兼容后摄可同时保存同次曝光的 DNG 与 JPEG。K80 实拍并解码两种文件，普通录像实测平均 60.10 fps。

## RedMagicEMU 0.6.6-beta

- 修复原厂相机因 GLES 外部纹理预览导致的闪退。
- 接入 Vulkan Android 窗口和交换链，让绘制命令直达宿主渲染线程。K80 跑通安兔兔标准版 GPU 场景。
- 对支持相应解码器的 Android 16 宿主接入 H.264 和 HEVC 硬解，修复媒体 Binder 文件描述符泄漏。
- 汇入首页导入进度、实例设置和数据重置。浮动菜单增加屏幕常亮。

[0.6.6-beta 发布说明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.6-beta)

## RedMagicEMU 0.6.5-beta (unreleased)

- 首页实例卡片显示导入进度。大 ZIP 可直接从宿主存储读取，解包与校验并行推进，保留条目、大小、CRC、分区结构和指纹检查。
- K80 导入 12.41 GiB 固件约用 75 秒，首次启动另计约 103 秒。

## RedMagicEMU 0.6.4-beta (unreleased)

- 改版首页和关于页，首页集中展示实例、目标固件和常用操作。
- 实例内存提供 4、6、8、12、16 GB 五档，导航键和垂直同步跟随实例设置，下次启动时应用保存值。

## RedMagicEMU 0.6.3-beta (unreleased)

- 在实例设置中加入 Guest 数据重置，保留固件、实例选项和共享文件，运行中禁止重置。
- 适配原厂 `/cache` 相对链接。K80 的隔离实例重置后重新进入原厂欢迎页。

## RedMagicEMU 0.6.2-beta

- 常规 APK 自动接入宿主 Vulkan 和原生 OpenCL。支持 FP16、线性缓冲区异步读回和非阻塞上传。
- K80 的 Adreno 750 跑通 Geekbench 6 Vulkan 和 OpenCL。性能浮窗将宿主进程和驱动内存纳入显示。

[0.6.2-beta 发布说明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.2-beta)

## RedMagicEMU 0.6.1-beta (unreleased)

- 排查安兔兔 3D Lite 的 Unity 崩溃，处理 GLES 像素存储、缓冲映射和重复绑定问题。隔离测试包在 K80 跑完两个 Unity 场景。
- 搭建宿主 Vulkan 服务、Guest 协议和计算链路。窗口、交换链与呈现协议在此阶段进入离线测试，后续版本接入运行环境。

## RedMagicEMU 0.6.0-beta

- 实例设置和浮动菜单加入电池同步，支持跟随宿主或手动设置电量和充电状态。
- 加速度计桥接扩展至陀螺仪、磁力计和宿主位置。修正传感器标志与定位入口，恢复原厂指南针。
- 打通虚拟 LTE 数据，修复关闭虚拟 Wi-Fi 后仍可联网的问题及相关启动崩溃。恢复前后摄与普通拍照。
- 收敛为单一应用包 `app.redmagicemu`，提供美式英语、英式英语、简体中文、台湾繁体和香港繁体，其他系统语言回退到美式英语。新实例采用 75% 默认布局。

[0.6.0-beta 发布说明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.6.0-beta)

## RedMagicEMU 0.5.0-beta (unreleased)

### 第一轮裁剪

- 围绕指定 NX809J 固件整理引擎、导入链和功能依赖，移除废弃原型。

### 第二轮裁剪

- 保留实例数据重置和已配对的图形资源，删去无实际入口的旧实例删除路径。最终选用约 41.6 MiB 的阶段包。

## RedMagicEMU 0.4.x (unreleased)

### RedMagicEMU 0.4.8-beta (unreleased)

- 验证原厂相机取景、前后摄切换、浏览器调用、拍照和 DCIM 保存。后续在 0.6.6 修复正式包的预览闪退，在 0.6.7 扩展原生尺寸和录像。

### RedMagicEMU 0.4.7-beta (unreleased)

- 快速关机时，正在进行的系统铃声索引会正常取消。
- Guest 增加 60、90、120、144 Hz 固定刷新档，并验证四档切换。

### RedMagicEMU 0.4.6-beta (unreleased)

- 第一轮梳理原厂 Sensors multi-HAL 的加载路径，整理宿主采样时间和 Guest 事件接口。
- 第二轮打通三轴加速度计。Guest 系统和普通应用都收到随手机晃动变化的读数。

### RedMagicEMU 0.4.5-beta (unreleased)

- 移除容量滑块，保留现有实例的存储配置。

### RedMagicEMU 0.4.4-beta (unreleased)

- 第一轮建立开放热点、状态通知和以太网交接原型。
- 第二轮对齐 WLAN 设置页、快捷面板和状态栏，修正 DNS 和启动顺序。退出飞行模式时恢复先前的 Wi-Fi 开关状态，清理浮动菜单入口。

### RedMagicEMU 0.4.3-beta (unreleased)

- 为模拟基带补充 LTE 信号上报。Guest 状态栏显示信号格，飞行模式往返后可恢复。

### RedMagicEMU 0.4.2-beta (unreleased)

- 为新实例自动准备指定鸣潮主题的息屏兼容数据。实测人物前景位置、唤醒动画和切换主题后恢复。

### RedMagicEMU 0.4.1-beta (unreleased)

- 适配 NX809J 的 `ztecfg`，恢复指定鸣潮主题的原厂壁纸与图标。
- 清理旧基带入口，按实例生成虚拟 IMEI，系统界面显示虚拟 IMEI SV。

## RedMagicEMU 0.3.0-beta

- **1A** 从原厂桌面启动基线出发，检查显示、触摸、文字输入、共享存储和 APK 安装。
- **1B** 修复文字输入路径，接通模拟短信和来电，修正接听、挂断后第二次来电失败。
- **1C** 通过以太网联网和宿主音频播放回归，补齐原厂铃声索引和默认铃声设置。
- **1D** 补齐录音、媒体与应用回归，修复普通振动桥。连续来电、计时器振动、网页和音视频场景进入本版。

[0.3.0-beta 发布说明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.3.0-beta)

## RedMagicEMU 0.2.0-alpha

- 修正正常启动误拉起充电服务的问题，处理目标固件的电话服务依赖，Guest 首次进入原厂桌面。
- 实机跑通全新导入、覆盖安装、Guest 重启和主动关机，以及宿主菜单停止。

[0.2.0-alpha 发布说明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.2.0-alpha)

## RedMagicEMU 0.1.0-alpha

- **1A** 建立 RedMagicEMU 应用标识、图标、项目说明和共享目录。
- **1B** 建立指定 NX809J 固件的 ZIP、分区和指纹检查，修正 ARM64 实例的 N64 引擎选择。
- **1C** 首页收敛为单实例，加入导入取消、暂存清理和导入中断提示。

[0.1.0-alpha 发布说明](https://github.com/poohpiggy/RedMagicEMU/releases/tag/0.1.0-alpha)

## S0 · 基线审计

- 以 AEmulator 为基线，梳理固件导入、Guest 启动和宿主桥接入口。配置 JDK 21、SDK 36 和 NDK r28，构建首份基线 APK。

## AEmulator 0.0.0.3 (upstream)

### Emulation
- New native arm64 engine: Android 5.0–17 and GSIs (8–17) run directly on the phone; Android 2.3–4.4 runs through QEMU.
- 64-bit firmware support; 32-bit programs inside them run through qemu-arm.
- Full Android 8 support, including GSIs.
- zygote and mediaserver crash much less often (binder fixes, system_server deadlock fixed).
- `/system` is read-only for the guest, so precompiled framework code is no longer lost.
- Faster first boot of Dalvik firmware (Galaxy S III: 104 s → 59 s).
- Option for hardware 32-bit execution (AArch32) without a translator, with QEMU as the fallback.
- GLES 1/2/3 through SwiftShader.

### Firmware and vendors
- TouchWiz: `ro.tvout.enable=false`, `ro.SecEDS.enable=false` and binder patches.
- Qualcomm firmware: removed `qti_permissions.xml`, `com.qti.dpmframework.xml`, `com.qualcomm.location`, `qcrilmsgtunnel`, `dpmserviceapp`, `DiracAudioControlService`, `DiracManager`.
- Infinix (AOSP / XUI / XOS) and Tecno (AOSP / HiOS) detected by Android version.
- Sony: notification LED emulation.
- Fixes for MIUI, HTC Sense, TouchWiz, Xperia and AOSP. Tested on Nexus 4, Galaxy Nexus, Galaxy S II/S III, Redmi 1S, Desire HD, One M7, Nexus 5X, Phh 9, AOSP GSI 17.

### Import
- New formats: 7z, RAR, `.pac`, yaffs2, rfs, EROFS, `super.img`, APEX, `.tar.gz`.

### Features
- Internet works in more firmware.
- Sound fixed; microphone, camera, accelerometer and vibration added.
- Navigation bar: Back, Home and Recents work on every firmware.
- ADB over the local network and a built-in ADB terminal.
- Three-dot menu in a running system fixed: screenshots, screen recording, VNC, SMS, airplane mode, APK install and more.
- The menu button can be moved in a running system.
- Option to hide the LED indicator: in defaults for new systems and per system.
