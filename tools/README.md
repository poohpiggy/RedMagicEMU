# 开发工具

- [Guest 测试](guest-tests/)按 Android 版本提供测试 APK。[Android 1.5–4.4](guest-tests/android-1.5-to-4.4/build.sh)、[Android 5.0–8.1](guest-tests/android-5.0-to-8.1/build.sh) 和 [Android 9 及以上](guest-tests/android-9-plus/build.sh)各有构建脚本，共用测试代码和[宿主运行脚本](guest-tests/common/host/)。
- [诊断工具](diagnostics/)包含图形、媒体和 Guest 系统探针。[Guest 进程采样器](diagnostics/guest-profiler/build.sh)使用 Windows Android NDK 构建，NDK 目录可通过 `NDK` 或 `ANDROID_NDK_HOME` 指定。
- [系统覆盖包](overlays/)包含 [NX809J 相机配置](overlays/nx809j-camera/build.ps1)和[旧版 Guest 的任务缩略图设置](overlays/disable-task-thumbnails/build.sh)。构建脚本将产物写入应用的引擎资源目录。
