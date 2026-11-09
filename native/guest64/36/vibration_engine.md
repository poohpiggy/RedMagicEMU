# 独立振动引擎构建（尚未启用）

`build_vibration_engine.py` 只构建当前 Android 16 ARM64 引擎源码，并要求输出文件名为 `libaemu64_vibration_36.so`。它不构建 Binder 守护进程，不覆盖正常启动使用的 `libaemu64_36.so`。输出已存在且内容不同、或输出路径为符号链接时会拒绝替换；重新构建一致内容可重复执行。

使用 NDK 28.2.13676358，例如在仓库根目录执行：

```powershell
python native/guest64/36/build_vibration_engine.py --ndk "$env:USERPROFILE\.redmagicemu-tools\android-sdk\ndk\28.2.13676358" --output app/src/main/jniLibs/arm64-v8a/libaemu64_vibration_36.so
```

本次生成的独立引擎 SHA-256 为 `88945ca0bba34f8dff5410c75f15ea6b04ac5ce277673d99cb9ac48720088325`；当时现有标准引擎为 `72767cb685adcb2f88ce29147fc4ec2feab79495e19dbecaa99561f3f93818ce`，构建前后未变。以下是本次使用的项目源码摘要；源码或 NDK 更改后，输出摘要可能不同：

```text
77f9f52e3098af51d26e7191a8128b988e38e42d82b4393eefe753352d917cf7  start.S
2b85f8b883655ff5f9c75ccba4d6a1365a36750ae301c51bb40943a5d93a4c52  run64.c
ee09ee6a0717993e6d0293f4159f1a7e6ebefb01e57023c4d6ed2a7daa3135b3  self_cmdline.h
45f175122fc8cf494ffad789163db7235004473cd1fcbaa789939349492a1b5c  bclient.c
c0b28a10257006ee23fef97b4db49c25bb6f43b4adfb3468140f8860607371e5  bproto.h
01778f3be126a2164307b0486efcf05ed507c977f85210d7135b07816cc3eda3  binder_client_trace.h
962dd69e97f50b5a1fc8ce039d4db5a0e26e77e942ef6b1e71f0a8ddc503b4b2  vibration_fd_guard.h
ab1c8323083a02a06209f97e9705543ce4fff5e0bd9367535e979f2f15df0db2  vibrator_preload.h
```

电脑回归可设置 `RME_TEST_NDK` 为上述 NDK 目录后运行 `python native/guest64/36/tests/test_build_vibration_engine.py`；它在临时目录构建两次并比较内容，同时核对标准引擎未改变。

此文件尚未接入 Guest 启动。未来仅选择原厂振动服务时才应使用；该服务通过 `exec` 启动的后代仍可能继承独立引擎及进程限制。Android 设备上的提取、执行、Binder 兼容和振动均未验证。需要回滚时，先确认 Guest 已停止且独立文件的摘要与上述一致，再仅移除该独立文件；不要移除标准引擎或修改原始固件。
