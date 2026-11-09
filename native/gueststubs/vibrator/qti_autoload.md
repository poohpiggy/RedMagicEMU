# 原厂振动接口自动加载实验（未接入）

`qti_autoload.c` 仅供电脑侧实验：当 `RME_VIBRATION_SOCKET`、`RME_VIBRATION_HOST_PID`、`RME_VIBRATION_HOST_UID` 和 `RME_VIBRATION_EPOCH` 全部有效时尝试建立现有实验会话；参数无效或宿主握手失败时保留默认拒绝，并在标准错误输出固定失败信息（不包含参数）。正常卸载时尝试取消并关闭，失败也输出固定消息；进程异常终止时仍需要宿主检测连接/进程退出并取消。传参和宿主握手只验证连接对端，不是 Guest 应用权限认证。

禁止将该实验加入正常 Guest 启动：当前 `PidfdGuestLauncher` 先启动宿主侧启动器，若直接把 `LD_PRELOAD` 放在 `ProcessBuilder` 环境中，该构造器可能在启动器中提前执行。还未验证如何只给目标原厂服务加载、Android 运行时动态链接、Guest 权限或设备行为。

```sh
python3 native/gueststubs/vibrator/tests/qti_autoload_test.py
```

这只在 Linux 自造 vendor 库和进程上测试默认拒绝、参数边界、错误握手、一次 500 毫秒请求和正常退出时的取消，不执行原厂 Android 二进制或操作手机。

适配库可由 NDK 28.2 重建：

```sh
python3 native/gueststubs/vibrator/build_adapter.py --ndk <NDK目录> --output app/src/main/assets/engines/redmagic/vibrator-adapter.so
```

构建脚本不会覆盖内容不同的已有文件。同样，`TargetVibrationArtifacts.prepare` 仅在明确启用、固件参数匹配时将资产放入运行副本的 `vendor/lib64/libredmagic_vibrator.so`，并拒绝冲突、目录或符号链接；相同内容可重复准备。当前没有实现不同版本资产的自动覆盖或移除，且尚未连接正常启动。回滚时先确认运行副本中的文件内容与本资产完全一致，再仅删除该副本内该文件；不得修改原厂固件。
