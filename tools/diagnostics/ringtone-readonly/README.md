# 铃声只读诊断应用

独立的普通 Guest 应用，用于检查当前 Android 用户的三类默认铃声是否有 URI、其 MIME 类型是否为音频，以及 `WRITE_SETTINGS` 的权限状态和 `Settings.System.canWrite` 结果。界面只显示布尔值、目标 SDK、Android 用户编号和异常类型，不输出 URI 或铃声文件路径。

应用声明 `WRITE_SETTINGS` 以便检查普通应用的权限状态，但不会请求或授予权限，也不会写入设置。启动 Activity 对外导出以支持手动启动，不包含网络或录音权限；不会自动安装，也不集成到 RedMagicEMU。用户编号不是电话号码。状态检查不能证明实际铃声播放或原厂硬件 HAL 兼容。

## 电脑端构建与检查

需要 Python 3、Android SDK Platform 36、Build Tools 36.0.0、JDK 和自行准备的签名密钥。密钥只保留在本机，不要加入仓库。以下 PowerShell 命令使用环境变量传递本机路径，执行前将尖括号及其中的占位文字替换为实际密钥库路径和别名。设置 `RME_PROBE_STORE_PASS` 和 `RME_PROBE_KEY_PASS` 为密钥密码，避免把密码写进命令或仓库。输出路径必须不存在，建议放在系统临时目录。

```powershell
$env:RME_PROBE_STORE_PASS = Read-Host '密钥库密码'
$env:RME_PROBE_KEY_PASS = Read-Host '密钥密码'
python tools/diagnostics/ringtone-readonly/build.py --sdk $env:ANDROID_HOME --java-home $env:JAVA_HOME --keystore <本机密钥库路径> --key-alias <密钥别名> --output "$env:TEMP/rme-ringtone-readonly.apk"
python tools/diagnostics/ringtone-readonly/verify.py --sdk $env:ANDROID_HOME --java-home $env:JAVA_HOME "$env:TEMP/rme-ringtone-readonly.apk"
python tools/diagnostics/ringtone-readonly/test_verify.py --sdk $env:ANDROID_HOME --java-home $env:JAVA_HOME "$env:TEMP/rme-ringtone-readonly.apk"
Remove-Item Env:RME_PROBE_STORE_PASS, Env:RME_PROBE_KEY_PASS
```

如需其他 Build Tools 版本，两条命令均可传入 `--build-tools`。构建过程的中间文件位于临时目录；不应提交 APK、编译结果或密钥。检查脚本从 APK 自身读取 DEX，核对启动类、权限、允许的方法调用、对齐和签名；该静态检查不能替代审查源码或设备测试。
