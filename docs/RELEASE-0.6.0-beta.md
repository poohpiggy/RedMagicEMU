# RedMagicEMU 0.6.0 Beta

面向指定的 REDMAGIC NX809J 原厂 Android 16 / API 36 / ARM64 Guest，宿主型号不受此限制。需要自行提供对应固件；本项目不分发原厂固件。

## 本阶段

- 发行包收敛为单一 `app.redmagicemu`，不再生成 clone 或 v0003 变体。
- 实例设置支持“跟随宿主/自定义”电池控制，运行时浮动菜单提供同一套电池与电源状态控制；宿主电池插拔变化会实时刷新设置页。
- 运动传感器桥接覆盖加速度计、陀螺仪和磁力计，并同时提供宿主最后已知位置；原厂指南针的航向、磁场和经纬度显示已通过实机验收。
- 支持 Ethernet、虚拟 Wi-Fi 和虚拟 LTE 网络模式。关闭虚拟 Wi-Fi 后仍可联网的问题、相关 zygote 回归和虚拟 LTE 数据链路均已修复。
- 原厂相机前后摄、普通拍照和图库可见性已通过实机回归；实例默认显示调整为原厂布局的 75%，已有自定义显示设置不会被迁移或覆盖。

## 已知限制

- Beta 版本；长期稳定性、断连恢复和其他宿主型号未整体验收。兼容性不等同完整原厂硬件 HAL、DSP、RichTap 或硬件视频解码。
- GPS 经纬度来自宿主授权后的最后已知 network/GPS 位置，不表示 Guest 已获得独立 GNSS 卫星解算。
- 虚拟 LTE 使用 synthetic Radio 和宿主 Internet 通道，不代表真实蜂窝网络、IMS/VoLTE 或原厂 Radio HAL。
- 相机高级模式、部分浏览器调用 API、占用/断连恢复和其他宿主未完整验证；曝光与 ISO 为虚拟标称值。
- Guest 应用身份与振动权限目前不能可信校验；部分未支持振动请求或超过 5 秒的请求可能导致桥断开。
- 手机可能显示 16 KB 页对齐兼容警告。更多证据和边界见 [兼容矩阵](https://github.com/poohpiggy/RedMagicEMU/blob/0.6.0-beta/docs/COMPATIBILITY.md) 和 [进度记录](https://github.com/poohpiggy/RedMagicEMU/blob/0.6.0-beta/docs/PROGRESS.md)。

## 构建与安装

附件为 Debug APK，applicationId 为 `app.redmagicemu`，使用本机现有调试签名；与其他签名版本不保证可覆盖升级。原厂固件不包含在 APK 中。测试、构建和设备运行的覆盖范围应分别理解。
