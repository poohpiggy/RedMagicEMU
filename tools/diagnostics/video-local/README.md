# 本地视频诊断

独立 Guest 应用，不申请额外权限、不联网。构建时用 FFmpeg 合成 12 秒运动彩条及 800 Hz 提示音，编码为 640×360、24 fps 的 H.264 Baseline + AAC MP4，视频仅放入临时构建目录和输出 APK，不存入仓库。应用使用 Guest `MediaPlayer` 播放内置文件，显示并记录准备、播放进度、结束或错误。

需要本机 FFmpeg（含 libx264 和 AAC 编码器）、Android SDK Platform 36、Build Tools 36.0.0、JDK、签名密钥。输出 APK 必须不存在，建议放临时目录；不要提交 APK 或密钥。

```powershell
$env:RME_PROBE_STORE_PASS = Read-Host '密钥库密码'
$env:RME_PROBE_KEY_PASS = Read-Host '密钥密码'
python tools/diagnostics/video-local/build.py --sdk $env:ANDROID_HOME --java-home $env:JAVA_HOME --ffmpeg (Get-Command ffmpeg).Source --keystore <密钥库路径> --key-alias <别名> --output "$env:TEMP/rme-video-local.apk"
Remove-Item Env:RME_PROBE_STORE_PASS, Env:RME_PROBE_KEY_PASS
```

通过 RedMagicEMU 现有安装入口安装到 Guest 并打开。预期出现变化的彩色图案、播放约 12 秒提示音，状态随播放位置变化并最终显示 `COMPLETED`；点击 `REPLAY` 可重播。需由实际画面和听测分别确认视频与声音，播放器回调和时间进度不能代替。完成后卸载本 Guest 测试应用。该测试不证明其他编码、流媒体、所有应用或原厂视频硬件 HAL 兼容。
