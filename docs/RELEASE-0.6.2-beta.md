# RedMagicEMU 0.6.2 Beta

面向指定的 REDMAGIC NX809J 原厂 Android 16 / API 36 / ARM64 Guest，宿主型号不限。运行前请自行准备对应原厂固件。

## 本阶段

- 常规 APK 在 GPU 模式下自动启动宿主 Vulkan 服务，并在 Android 启动前配置 Guest 客户端。
- Guest 应用通过实例专用连接调用宿主原生 OpenCL。FP16 能力取自宿主驱动。
- 线性缓冲区异步读回可使用共享槽。线性缓冲区、图像和矩形缓冲区的非阻塞上传会在 GPU 完成前返回。
- Adreno 750 上完成了 Geekbench 6 Vulkan 和 OpenCL、FP16 内核及异步上传回归。阶段测试的成绩和对应 APK 见[进度记录](PROGRESS.md)。

## 已知限制

- 图像和矩形缓冲区读回目前由宿主同步完成。
- 手机可能显示 16 KB 页对齐兼容提示。详见[兼容矩阵](COMPATIBILITY.md)和[进度记录](PROGRESS.md)。

## 构建与安装

附件为 Release 构建，包名 `app.redmagicemu`，Android `versionCode` 为 `12`。使用调试签名，覆盖安装要求签名一致。

APK SHA-256 `db85201bdf3b3825d1f7eddd03bbfb63ef489509b48fce7d8145278871309361`。
