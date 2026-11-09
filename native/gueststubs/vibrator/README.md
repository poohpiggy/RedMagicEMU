# 振动实验，禁止接入产品

此目录是未完成的实验代码，未接入应用构建或设备。当前模拟 Binder 的调用者 UID 不能用作可信授权，因此实验 HAL 和控制器默认拒绝振动请求；它们并没有实现 Guest 权限校验。不得安装、发布或接入宿主振动接口。

请以 `docs/PROGRESS.md` 和 `docs/COMPATIBILITY.md` 的振动条目为当前状态。目录清理因自动策略拒绝而未完成。

在具备 JDK 和本地 Android SDK 36、build-tools 36.0.0 的电脑上，可运行 `python native/gueststubs/vibrator/verify_offline.py --sdk <SDK路径>`。脚本验证 AIDL v2 快照的文件清单与 SHA-256、生成接口并编译实验代码，执行控制器和受限接口检查；所有生成物在临时目录，结束后不发布构建包。检查模拟系统 UID，验证实验代码仍拒绝输出；它不证明目标 ROM 实际接口、服务注册或权限隔离。

`VibratorRegistry` 是尚未接线的注册实验：只准备 `IVibratorManager/default` 的声明、占用及发布可见性检查。离线测试使用仅供测试的 ServiceManager，未向 Guest 注册服务。即使名称可见，也不能证明 HAL 可用或调用身份可信；现有 HAL 不能作为可运行服务注册。一次注册尝试后，调用方必须在失败或结束时退出整个服务进程，避免延迟完成的发布残留。

注册器的唯一观察结果为 `OBSERVED_UNVERIFIED`。相同的接口描述符可能来自其他 Binder，它不证明观察到的服务是本进程刚提交的对象，不能作为服务归属或身份认证；目标运行时的 Binder 对象比较语义尚未验证。
