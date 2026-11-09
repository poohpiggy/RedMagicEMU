# Guest AHardwareBuffer 格式诊断

独立 Guest 应用，不申请权限或访问网络。针对自己分配的 640×360 缓冲区，分别检查 YUV_420_888 (`0x23`) 和 YV12 (`0x32315659`) 在 CPU 读写用途、CPU 读写加 GPU 采样用途下的分配和 `lockPlanes`。仅记录阶段、返回码和 plane/stride；不读取或输出地址、像素及用户内容。GPU 用途仅是分配描述，不执行 GPU 渲染。

需要 Android SDK Platform 36、Build Tools 36.0.0、Android NDK、JDK 和本机签名密钥。不要提交密钥、APK 或 Guest 数据；输出路径必须不存在。

```powershell
$env:RME_PROBE_STORE_PASS = Read-Host '密钥库密码'
$env:RME_PROBE_KEY_PASS = Read-Host '密钥密码'
python tools/diagnostics/ahb-formats/build.py --sdk $env:ANDROID_HOME --ndk $env:ANDROID_NDK_HOME --java-home $env:JAVA_HOME --keystore <本机密钥库路径> --key-alias <别名> --output "$env:TEMP/rme-ahb-formats.apk"
Remove-Item Env:RME_PROBE_STORE_PASS, Env:RME_PROBE_KEY_PASS
```

通过 RedMagicEMU 的安装入口安装至 Guest，启动 `app.rme.probe.ahbformats/app.rme.probe.BufferActivity`；不传参数时按顺序执行四项。可传入整数 Intent 参数 `case` 单独执行：0 为 YUV_420_888 CPU 读写，1 为 YUV_420_888 CPU 读写加 GPU 采样，2 为 YV12 CPU 读写，3 为 YV12 CPU 读写加 GPU 采样。单独执行可区分某个调用阻塞时其余案例的结果。日志标签为 `RmeAHBProbe`，每个案例分别报告分配、锁定和解锁结果；未到达解锁步骤时不会将其报告为成功。测试后可卸载该 Guest 应用。

YV12 数值格式不在 NDK 的公开 AHardwareBuffer 格式枚举中；如果分配直接被拒绝，不能仅凭该结果认定调用已进入 mapper。该探针不证明视频或原厂硬件 HAL 完整兼容。
