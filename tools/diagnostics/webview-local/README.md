# 本地 WebView 诊断应用

独立 Guest 普通应用，不集成到 RedMagicEMU，也不声明任何权限。页面全部来自应用内置的 HTML；它不会访问网络、文件或内容提供者。应用显示 WebView 加载、脚本和按钮回调状态。加载或渲染失败会显示错误，30 秒内未运行脚本会显示超时。

## 构建

需要 Python 3、Android SDK Platform 36、Build Tools 36.0.0、JDK 和本机签名密钥。密钥和 APK 不要提交到仓库。输出路径必须不存在，建议放在临时目录。

```powershell
$env:RME_PROBE_STORE_PASS = Read-Host '密钥库密码'
$env:RME_PROBE_KEY_PASS = Read-Host '密钥密码'
python tools/diagnostics/webview-local/build.py --sdk $env:ANDROID_HOME --java-home $env:JAVA_HOME --keystore <本机密钥库路径> --key-alias <密钥别名> --output "$env:TEMP/rme-webview-local.apk"
Remove-Item Env:RME_PROBE_STORE_PASS, Env:RME_PROBE_KEY_PASS
```

## Guest 内检查

通过 RedMagicEMU 已有的 Guest APK 安装路径安装并打开该 APK。确认页面能看到 `STATIC CONTENT OK` 和 `JAVASCRIPT OK`；点击页面里的 `Tap here`，确认页面出现 `BUTTON INTERACTION OK`，蓝色顶部状态和紫色底部原生对照条变为 `JS + BUTTON OK`。状态或系统日志标签 `RmeWebViewProbe` 可用于排查失败；日志还记录根布局、状态条和 WebView 的屏幕坐标及可见区域，便于对照实际显示。如果应用启动即崩溃，还需检查系统崩溃日志。完成后可在 Guest 内卸载诊断应用。

可向启动 Activity 的 Intent 传入布尔 extra `native_only=true`，切换到不创建 WebView 的原生对照页面。对照页面使用同一个根布局和状态条，并在日志中以 `native-content` 标记内容区域；需对照页面的实际画面和各视图坐标，不能仅凭日志断定内容未被遮挡。

探针使用无 ActionBar 主题，并根据系统栏和显示切口区域设置内边距。传入 `legacy_layout=true` 可跳过内边距，用于复现未适配的边距行为；该模式仍使用无 ActionBar 主题，并非旧版 APK 的完全副本。可与 `native_only=true` 组合使用。日志的 `root-padding` 记录实际应用的内边距。

在一次 target SDK 36 的旧版探针检查中，页面顶部和顶部原生状态被标题栏遮挡，纯原生对照也有同样现象；同布局的 target 28 版本正常。当前探针改用了无标题栏主题并处理系统边距，因此无法原样重现旧版本的标题栏对照。这个对照不能单独确定遮挡根因。

这些结果只覆盖本地网页显示、脚本和页面点击，不证明远程联网、视频、普通应用或原厂硬件 HAL 完整兼容。
