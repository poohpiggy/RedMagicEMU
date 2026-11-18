# 0.5.0 内部迁移与回归

## 方向更新（2026-11-15）

项目方向调整为保留 `app.aemu` 内部命名，先审计裁剪候选。下文两项名称迁移均已撤回；显示适配代码的包名迁移在完成前也已撤回。工作区原有未提交改动已保留，工作区和隔离构建快照同步恢复；当时未删除候选代码或资产，后续裁剪见下文。原先两份迁移测试 APK 仅作为历史记录。

恢复命名后的隔离 standard Debug 构建成功，显示布局 3 项与系统启动准入 2 项定向测试通过；未重新完成全量测试，也未安装或实机回归。构建复用相机实验二进制，限制仍见下方。恢复命名的测试包为 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-keep-aemu-checkpoint.apk`，SHA-256 `42fa5dc73fcc6b34df598f5dd690537eb6467bccb704b02a53d613a84fcd3588`；与相机实验包的非签名 APK 条目仅 `classes7.dex` 不同，不据此断言运行行为等价。包名、调试证书未变，版本仍显示 `0.4.0-beta`。

### 三组裁剪（2026-11-15，设备回归待补）

- 在 `codex/0.5.0-target-prune` 裁剪三组旧组件：四个 Honeycomb 图形资产、旧容量滑块辅助对象与对应三项测试、已无入口的旧上游更新实现和对话框。两个 Activity 仅删除各三条相关导入；`diskMb`、存储路径、设置值、其他翻译、Manifest、安装权限及 FileProvider 均保留，原有文件的其他未提交内容未动。
- 目标 NX809J 正确识别为 API 36 时，`HcHwRender.install` 在 API 11–13 之外于读取资产前返回；保存的 Guest 元数据在异常或陈旧状态下未证明不可为其他值，故不将此说成所有状态的不可达保证。预编译、JNI、反射、Guest、协议、资产索引及压缩档按名称扫描未发现这三组的额外引用；不能排除未知动态拼接。四个资产历史来源提交为 `0cf76bc`，`HcHwRender` 原有来源说明、根许可证和项目来源声明仍保留。
- 修改前 10 个受影响文件（含未跟踪的容量辅助代码、测试和既有修改的 Activity）已逐个备份并校验至 Git 外 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-three-group-prune\`；恢复时只将被删除的 8 个文件按原路径复制回来，并从备份中逐行恢复两个 Activity 的六条导入，保留期间其他修改。不要整树重置。手机此前确认启动的迁移测试包仍保留，恢复命名的裁剪前构建 `RedMagicEMU-keep-aemu-checkpoint.apk` 也保留但未实机验证；均须正常关闭 Guest 后覆盖安装。
- 全量 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 首次 724 项、2 失败、90 跳过：跨进程锁测试是先前已记录的不稳定项；`TargetVoldArtifactsTest` 首次在 ZIP 第 10 字节比较失败，测试两次生成带当前时间的 ZIP，独立探针复现跨 ZIP 两秒时间戳边界的同位置差异，单项重跑通过，但不能证明原次失败唯一原因。重跑全量测试和构建成功：724 项、0 失败、0 错误、90 跳过；比此前少 3 项是随滑块辅助代码移除的测试。这不代表上述不稳定测试已经修复。
- APK 索引由 1633 项变为 1629 项，恰好删除 4 个目标路径，其他索引及 blob 内容一致；DEX 类集合中 61 个消失的类都属于滑块辅助代码、旧更新模块及其生成类。为消除增量打包留下的空隙重新打包后，以内容摘要确认 DEX 字节集合与测试构建一致（文件名有重排）。裁剪前后 APK 分别为 79,171,670 与 77,841,234 字节，减少 1,330,436 字节；其中包含消除旧打包空隙的收益，不全归因于资产删除。
- 测试 APK：`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-pruned-test.apk`，SHA-256 `60cf8efd37ac7dac5372ac83cffefdfd18763c0955e836ec2a3b1c4a88b35d89`。包名 `app.redmagicemu`、调试证书与检查点一致，版本仍为 `0.4.0-beta`，继续包含相机实验二进制。交付时尚未安装、启动或执行设备回归；待验证项为启动、画面/触摸、存储、联网及相机等下方清单。发现回归先定位或恢复检查点，不继续裁剪。
- 设备验收记录：该包完成一次基础回归，但没有逐项说明具体测试范围；下方每一项不据此单独标记为通过。

### 四组后续裁剪及 ARM32 探针（2026-11-15，设备回归待补）

- 在 `codex/0.5.0-legacy-prune`，根据依赖审计删除旧多实例界面、`libqemu_gb.so`、四个旧系统相机 HAL 资产，以及未接线 AOD 画布原型的三个源码文件和两份测试。Recovery 辅助资产、正式 AOD 导入及代理、目标相机资产、`libqemu_arm.so` 和 KK 图形服务保留。相机源代码与构建脚本仍在；执行旧相机构建脚本可能重新生成被裁剪的旧资产。
- 修改前 11 个删除文件和原 `GuestVmEngineTest.kt` 已逐项备份并校验至 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-legacy-prune\`；包含工作区原有未跟踪的 AOD 原型和测试。回滚源码时仅按原路径恢复这些文件，并仅撤销本轮新增的 GB 测试，不覆盖其他未提交更改。通过基础回归的裁剪前 APK 保留于 `RedMagicEMU-0.5.0-pruned-test.apk`；回退安装前正常关闭 Guest。
- 目标 `GuestVm` 对旧 `Engine.GB` 元数据调用 `effective()` 后选择 N64 和 `libaemu64_36.so`；新增测试验证该选择、原始元数据不变以及无旧 GB 可执行文件时仍选择 N64。隔离负向对照令 GB 元数据错误地继续选择 GB，新增测试按预期失败；恢复源码后测试通过。旧 `Engine.GB` 兼容分支代码仍在，但被移除的 GB 可执行文件不再随 APK 提供；上述结论仅覆盖现有目标入口。APK 内 56 个 ELF 的 `DT_NEEDED`/`SONAME` 和压缩成员定向扫描未发现其他 GB 二进制引用，但不能排除未知动态调用。
- `libqemu_gb.so` 是不可确认等价重建来源的预编译文件，原件及摘要已保存在检查点；上游提交、版权和二进制中提取的许可声明见 [GB QEMU 来源及声明](LEGACY_QEMU_GB_NOTICES.md)，原始许可证与项目来源说明保留。
- 全量测试首次为 719 项、1 失败、89 跳过，失败为已有不稳定的跨进程锁测试；重跑 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 成功，719 项、0 失败、0 错误、89 跳过。相比此前 724 项，移除原型 6 项、增加 GB 测试 1 项；跳过数减少的一项来自被移除的原型测试。重跑成功不代表锁测试问题已修复。
- APK 资产索引 1629→1625，恰好删除四个旧相机路径，其余索引和 blob 内容不变；原生库中仅删除 `libqemu_gb.so`，包括 32 位翻译器的其余库内容一致。DEX 类集合仅减少旧界面及 AOD 原型的 60 个类。重新打包后 DEX 字节集合与完整测试构建一致；裁剪前后 APK 分别为 77,841,234 与 76,107,833 字节，减少 1,733,401 字节。
- 测试 APK：`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-legacy-pruned.apk`，SHA-256 `4f759040abc1bd810e2044afa1b1fbec0659eea19890e674f105f4f5f29e749a`。包名 `app.redmagicemu` 和调试签名未变，版本仍显示 `0.4.0-beta`；相机实验二进制仍随隔离包提供，既有限制未改变。
- 独立 ARM32 探针：`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-ARM32-Guest-Probe.apk`，SHA-256 `b07301f32daf139783e52cc75f307af40785e1419779b6a1ef464a8898ddb856`；包名 `app.redmagicemu.arm32test`，无权限或网络请求。APK 只包含 `armeabi-v7a` ELF32 ARM 原生库，实际 JNI 调用显示进程位数、指针位数及计算结果；源码和重建说明在相邻 `arm32-probe`。此探针及裁剪 APK 尚未安装或实机运行，应先安装主应用并启动目标 Guest，再在 Guest 内安装探针验证。探针仅覆盖基本 32 位执行与触摸，不证明完整兼容或图形性能。
- 设备回归记录：32 位探针无法安装；上一轮裁剪包完成过基础验证，但未逐项列明范围。此前一次只读 ADB 查询看到的手机安装包为 `60cf8efd…`，不是 `4f759040…`；该观察只反映查询当时状态，不否定后续实机验证。

### 原厂 ROM 32 位检查及 API 25 执行器裁剪（2026-11-15，设备回归待补）

- 设备 `_ztecfg_codex` ZIP 与电脑 `_ztecfg` ZIP 的 SHA-256 均为 `5b1a46705baf0032c2583e5ea96159dc3d87137af791d084405c25658d5178ba`；ZIP 内解压流 `super.img` 与原厂 `images/super.img` SHA-256 均为 `17c624a874accb94b8049654caebbcfa1c21a32f269ead5894e737ff1965c511`。原厂 system/vendor ABI 属性只声明 `arm64-v8a`、`abilist32` 为空，vendor `ro.zygote=zygote64`；system 无 `app_process32` 与 32 位 linker，runtime APEX 只发现 64 位 linker/lib。运行副本同样缺少 `app_process32` 和 32 位 linker；GuestVm 会重写运行时 ABI，不能将其运行属性当成原厂属性。以上支持原厂固件未提供普通 32 位应用运行环境，但此前观察到的安装失败未取得包管理器具体错误码。
- 原厂 system/vendor/product/system_ext/odm 中按可执行权限或 bin 路径检查的 ELF 为 501 个 ELF64、0 个 ELF32；未穷举所有 APEX、APK 和动态生成内容。全部外层文件中仍有 17 个 ARM ELF32（16 个非可执行权限的共享库和 1 个固件文件）；不得据此删除原厂文件或认定模拟器翻译器 `libqemu_arm.so` 必然无用。
- 本轮只从 APK 来源中删除 `app/src/main/jniLibs/arm64-v8a/libaemu32_25.so`：它是 API 25 的 32 位 Guest 执行内核；目标为 API 36/ARM64，正常目标启动不会自动选择。54 个 JNI ELF 动态依赖和资产/压缩档名称扫描未发现额外直接引用，不能排除未知动态调用或异常元数据。`libqemu_arm.so`、原厂固件、`native/guest32/25` 源码、构建脚本及许可证均保留；源码能否逐字节重建该预编译文件尚未证明。
- 删除前二进制已备份至 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-open32-25\app\src\main\jniLibs\arm64-v8a\libaemu32_25.so`，SHA-256 `c590a05af4550ac39626407ff6440e363d05b428465735eebb1bd083be6af0a8`，历史首次加入提交 `c9bec85271d384ddbe5c98de3ed476ce6a6d8a6f`。回滚只需将该备份按原路径复制回工作区及隔离构建源并重建；运行 APK 回退使用 `RedMagicEMU-0.5.0-legacy-pruned.apk`，覆盖前先正常关闭 Guest。
- 全量单元测试强制重新运行及重跑各一次，均为 719 项、1 失败、89 跳过；随后目标启动和锁测试的定向组合为 12 项、1 失败、2 跳过，失败均为此前已记录的不稳定 `InstanceRuntimeLeaseTest.anotherProcessBlocksAndDeathReleasesLock`。不能记作全量测试通过。独立 `:app:assembleStandardDebug` 成功；APK 比上一版少且只少一个库条目，其他原生库和资产内容一致，包名与调试证书相同。完整重编译使 `classes6.dex` 字节改变：两版类集合均为 679 类，但归一化后仍有 4 个 Compose 类的指令差异，原因及行为等价性未证实；因此本轮 APK 不是仅有一个二进制条目变化，须重点回归主界面、设置及 Guest 菜单。
- 待验证 APK：`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-no-open32-25.apk`，SHA-256 `5dbd70c35712990df7b0596521d22b6cd4cf417fd67dff1d34a506a8be9ecb83`。裁剪前后 APK 分别 76,107,833 与 76,072,042 字节；版本仍为 `0.4.0-beta`，相机实验库保留。尚未安装、启动或实机回归；覆盖安装前需正常关闭 Guest，安装后验证启动、主界面、设置、画面及已支持功能。出现回归先恢复上一 APK，不继续裁剪。
- WSL2 Ubuntu 26.04 的 Linux 文件系统隔离源码快照使用 Linux OpenJDK 21.0.2、Gradle 8.14.2、Linux Android Build-Tools 35.0.0 和 Android 36 平台重新编译并运行 `:app:testStandardDebugUnitTest :app:assembleStandardDebug`，47 个任务实际执行，构建成功；719 项测试中 0 失败、0 错误、3 项跳过，`InstanceRuntimeLeaseTest` 8 项全部通过、无跳过。3 项跳过分别来自 `TargetRealTreeTest` 1 项及 `TargetAodForegroundTest` 2 项。WSL 使用 Linux 文件系统和 Linux JVM；这与 Windows 此前两次完整运行各有 1 项锁测试失败形成对照，但尚不能证明 Windows 失败的根因，也不代表 Android 实机行为。
- WSL 原始 Debug APK 默认使用了 Linux 侧新生成的调试证书；为保持现有安装身份，使用原有 Windows 调试密钥对 WSL 产物重新签名，证书 SHA-256 为 `494eb713e0b14a19cb11d08e8cabb0a0b0fc25c341e57eaf0581c122527be389`，与上一包一致。重签名 APK `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-WSL-test.apk` SHA-256 为 `7c13b9cb515ed748b3671fc84be73e860de074ed54655f5389a193bcb67f1b5e`；与 Windows APK 所有非签名 ZIP 条目逐项字节一致。此包尚未安装或实机回归；应用包名和版本号未变。
- 设备验收记录：该版完成一次基础回归；未逐项说明测试内容，不据此勾选全部功能回归或扩大相机验收范围。

### 旧 GB/KK 引擎批量裁剪（2026-11-15，设备回归待补）

- 本轮将 GB/KK 和其他非 API 36 版本中确认安全的旧引擎一并处理。正常目标启动 `GuestImage.effective()` 选择 N64；仅按此边界移除 `libqemu_kk.so`、`libbinderd_gb.so`、`libglserverd_gb.so`、`libdhdrun_gb.so`、`libbinderd_kk.so`、`libdhdrun_kk.so`，以及 `engines/gb/` 的 `dhd.props`、`gralloc.default.so`、`libGLES_dhd.so`、`libashmemshim.so`、`libaudio.legacy.so`。旧 GB/KK 分支不再具备完整可运行二进制。`doRecovery` 也引用 KK 程序，但当前 `requireSystemBoot` 在进入前拒绝 Recovery；将来恢复 Recovery 必须同时恢复相关旧组件。
- 其他 15 个非 36 版本的 30 个 N64 runner/Binder **暂保留**：`GuestImage.fromJson` 在 API 元数据缺失时回退 19，现有固件准入主要检查指纹，不能证明所有旧实例的元数据都选 36。直接删除可能改变旧实例启动；本轮不改启动行为或屏蔽错误。非 36 的 `n64-*` 资产索引仍被 `TreeFixer` 枚举以识别和恢复旧运行树，亦保留。`engines/kk/aemu-stubs.jar` 为目标启动共享组件，旧音频策略恢复也使用两个 KK 资产；`libqemu_arm.so`、`libglserverd_kk.so`、`libglbuf.so`、Manifest 注册的 `libdhdslot.so` 均保留，不能仅按旧名称删除。
- 原 KK QEMU 是不可确认等价重建来源的预编译文件，原件备份于 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-kk-prune\`，SHA-256 `8fe3c6f31236bb5cd718e26e01bf325528eba7646fe56ec9f0b17c2582f8fd68`。其 QEMU 版本、上游来源和所含版权声明保留在 [QEMU 来源记录](LEGACY_QEMU_GB_NOTICES.md)。另 10 个删除文件及候选 30 个保留文件的修改前快照和 SHA-256 清单位于 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-batch-engine-prune\`；原始许可证、来源和源码均保留。恢复源码时按两个备份目录的精确原路径只恢复本轮 11 个文件，不要重置工作树；恢复 APK 可在正常关闭 Guest 后覆盖安装本轮前的 `RedMagicEMU-0.5.0-WSL-test.apk`。
- WSL 原有 `GuestVmEngineTest` 先通过；隔离负向对照强制旧 KK 元数据错误地保留 KK，测试如预期失败，恢复源码后通过。WSL `:app:assembleStandardDebug` 成功，全套 `:app:testStandardDebugUnitTest --rerun` 实际执行 719 项，0 失败、3 跳过，跨进程锁 8 项全部通过。扫描源码、预编译、压缩档和 JNI ELF 的动态依赖未发现上述旧文件在目标正常入口的额外消费者；静态检查不能排除动态拼接或外部调用。
- APK 索引只移除 5 个 GB 路径，其中 4 个独占 blob 被移除，另一个与 KK 共享的 blob 保留；其他索引不变。与本轮前 APK 比较，非签名内容除上述 6 个原生条目、GB blob 和 `engines.idx` 外完全相同，包含已验收相机的两端图形桥、两份 HAL 及目标 Vold 资产均保留。重打包内容与测试构建一致；使用原有调试证书签名，包名 `app.redmagicemu` 和版本 `0.4.0-beta` 不变。
- 测试 APK：`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-old-engines-pruned.apk`，SHA-256 `2b33778e054026f959869ebd092e80f2e1f58756b6b58069e01a43794698d395`。前一 APK 为 76,153,199 字节，本包 74,374,686 字节，减少 1,778,513 字节。尚未安装、启动或进行设备回归；覆盖安装前需正常关闭 Guest，安装后检查已有实例启动、界面、联网、相机和息屏等已支持功能。发生回归先恢复上一 APK，不继续裁剪。
- 相机已通过现有使用范围验收；测试 APK 的四份图形/HAL 产物与相机实测包一致。普通工作区的对应四份二进制仍不同，直接普通构建不能替代该隔离包。`native/grallocwrap/camera_read.c`、头文件及两份测试是未接入的另一种帧协议原型，当前不打包；本轮为保留后续故障诊断材料而未删除。验收用图形桥源码和诊断构建器同样保留。

### 批量裁剪与测试包（2026-11-15）

- 本轮合并裁剪此前汇总的旧 GB/KK 桥接、9 项旧 KK 资源、Recovery 辅助库、两份已关闭的实验振动库、非 36 的 N64 资源及 30 个运行库、旧更新设置和无入口 Recovery 设置界面、未接线输入及相机原型，以及 `libqemu_arm.so`。`app.redmagicemu`、原有调试证书和 `versionCode=8` 保持不变，`versionName` 为 `0.5.0-beta`。不改实例元数据或运行器的启动选择来掩盖缺失库。
- 旧 N64 资源删除了 1,397 个索引项；旧实例运行树的恢复逻辑现在从 `engines/history/` 两份名称清单读取历史 kernel/own 与 GSI 名称，再合并现存名称；缺少清单会报错。与上一 APK 比较，普通/own 的 194 个名称、GSI 的 18 个名称集合一致；这不等于已在设备上验证旧实例恢复。原有目标 API 36 和共享图形的 182 条索引未变。旧值或缺失元数据仍可能选中已删除的非 36 运行库，导致旧实例无法启动。
- 删除 `libqemu_arm.so` 是实际行为风险：没有翻译器时现有 `GuestVm` 会跳过 32 位服务，Guest 内部 `exec` 32 位程序也会失败。此前原始 ROM 的分析未穷举嵌套 APEX 和 APK，无法证明目标运行链完全不依赖它；此前观察到的 32 位应用无法安装也不能证明这一点。旧 GB/KK 与 Recovery 不再完整；实验振动源码和构建工具仍在，但重新启用实验链需要先恢复或重建其二进制。直接振动与已验收相机链保留。
- 删除前备份：历史 N64 资源与 30 个运行库位于 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-n64-prune\`；三个旧原生库、9 项 KK 资源及 Recovery 库位于同目录下 `before-20261115-legacy-native-batch\`；未接线原型及改动前源码和资源位于 `before-user-batch-small-prune\`。各备份已逐项校验 SHA-256。旧 N64 资产版权与许可见 [来源记录](LEGACY_N64_ASSET_NOTICES.md)，QEMU 原始声明见 [QEMU 来源记录](LEGACY_QEMU_GB_NOTICES.md)。恢复源码应只按清单恢复相应原路径并撤销对应代码修改，不得重置其他未提交内容；恢复旧 APK 前先正常关闭 Guest，运行树或用户数据的变化不能保证仅靠覆盖旧 APK 回滚。
- 隔离 WSL 构建 `:app:testStandardDebugUnitTest :app:assembleStandardDebug --rerun-tasks` 成功，47 个任务执行；713 项测试、0 失败、0 错误、3 跳过。之后只移除设置源码末尾多余空行，并重新执行测试和构建成功（7 个任务执行、40 个最新）。历史名称定向测试和 APK 中清单读取通过。首次调用因隔离快照的 `gradlew` 为 CRLF 报解释器错误，只在隔离快照转换行尾后重跑；未修改工作区脚本。自动测试不是设备回归。
- 本轮 APK 为 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-beta-pruned.apk`，SHA-256 `d2306cc0b37c80713e515f9999ae3d301f2964265704f8b2eda824f96438e848`，60,324,883 字节；比 `old-engines-pruned.apk` 少 14,049,803 字节。APK 签名验证成功，证书 SHA-256 `494eb713e0b14a19cb11d08e8cabb0a0b0fc25c341e57eaf0581c122527be389`。APK 原生库从 48 个变为 13 个，计划移除的 35 个库均不存在，目标 36 运行库及正常振动库保留；相机宿主图形库及 Guest 图形/HAL 共四份二进制与相机验收快照一致。
- 设备验收记录：该包完成一次基础回归；未逐项说明测试的功能和场景，故不能据此声称下方所有回归项均已逐一覆盖。出现回归应先停裁剪并定位或安全恢复。普通工作区四份相机二进制与验收快照仍不同，不可声称普通构建可复现本包。

### 图形、旧视频链与目标导入工具裁剪（2026-11-15）

- 本轮将此前列出的旧图形、32 位资源和旧视频编码链一起裁剪，并按固定固件 ZIP 的实际依赖决定旧导入工具。`packEngines` 从 APK 索引中排除 `n64-gl/system#lib64#egl#libGLESv2_swiftshader.so`，原文件仍保留供旧构建使用；API 36 的同名后层文件保留，构建时检查其存在。8 个 N64 API 36 的 32 位 system/vendor 资产已删除，历史名称仍在恢复清单；旧实例停放文件可能因此恢复，设备回归尚未验证。源码中的构建脚本仍可能重生成这些预编译资产。
- 删除 `libglserverd_kk.so`、`libglbuf.so`、`libaemum4v.so` 与 `engines/common/libOmxVidEnc.aemu.so`；同步移除仅供旧 OMX 编码链使用的 `VideoEncoderFeed`、GuestVm 的 socket 创建/启动/停止和旧 API 安装分支。目标 APK 的其余资源和库按名称扫描未发现旧 socket 的其他消费者；原始目标 `super.img` 直接字节扫描也未发现该 socket 名，但未穷举嵌套压缩组件或动态拼接。相机录像和视频需设备回归。
- 指定原始 ZIP 包含 `super.img`、`ramdisk.img`、`boot.img`、`init_boot.img`、`vendor_boot.img`、`README.txt`、`ztecfg.img`，无 7z/RAR/payload；当前 `Importer.import` 经 `TargetArchive` 和 `TargetPackageTree` 处理它，旧通用 `handle` 无公开调用入口。删除 `libpayloaddumper.so`、`libsevenzip.so`、`libpdgwrap.so`，保留 ZIP 验证、解析器、旧源码及第三方许可。旧通用导入实现不再拥有上述工具，若恢复其他格式必须重新评估。隔离 JVM 使用项目生产 `TargetArchive.validate` 对真实 ZIP 成功运行一次，验证范围是压缩包结构，不是完整解压、实例导入或 Android 设备实测。
- 移除前备份及 SHA-256 清单分别位于 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-050-graphics-prune\`、`before-video-gpu-prune\`、`before-20261115-target-import-prune\`，所有条目校验通过；原文件仅按需恢复，避免覆盖其他未提交工作。图形/视频源码与原有许可保留；旧 SwiftShader 文件本身未删除。
- WSL 隔离快照中 `:app:testStandardDebugUnitTest :app:assembleStandardDebug --rerun-tasks` 成功，47 任务执行；完整测试重新统计为 713 项、0 失败、0 错误、3 跳过。额外真实 ZIP 结构检查在隔离快照执行并通过，临时测试随后移除；未执行真实固件完整树提取或设备测试。
- 测试 APK：`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-beta-graphics-pruned.apk`，SHA-256 `070f18d8c8c5de9b164984868dad4c4f7f578d8ae08f405a56711e9ac7859fee`，43,636,341 字节，比已通过基础验证的上一 APK 少 16,688,542 字节。`app.redmagicemu`、`0.5.0-beta`、versionCode 8 和既有调试签名证书保持一致。APK 对比确认仅移除预期的 10 个资产索引和 6 个原生库，剩余索引内容一致；四份相机验收二进制相同。DEX 因源码改动及重编译发生变化，不能据此声称 APK 其余内容完全不变。
- 设备验收记录：该包完成一次基础回归；未逐项说明测试了哪些功能，不能据此推定相机录像、新实例导入和旧实例恢复均已覆盖。发生回归先停止裁剪、定位或回滚；覆盖安装旧 APK 不保证撤销已修改的运行树。

### 铃声初始化内存原型裁剪（2026-11-15）

- 本轮继续裁剪并提供 APK；仅移除 `FirstRingtoneInitState.kt` 与专用 `FirstRingtoneInitStateTest.kt`。原型自身说明它不实现或验证持久化及写入，同名搜索只发现原型和专用测试；基线 APK 中的命名符号仅在该类及内部类定义中，未见其他类的直接调用，JNI 库及资产也未发现命名引用。实际使用的 `FirstRingtoneInitStore`、扫描、持久化和铃声逻辑均保留。静态搜索不能排除动态拼接引用。
- 原始两个文件已逐项复制并校验 SHA-256 至 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-ringtone-prototype-prune\`，附清单；回滚按原路径复制，保留其他未提交工作。基线为回退后已安装的 `RedMagicEMU-0.5.0-beta-graphics-pruned.apk`（SHA-256 `070f18d8c8c5de9b164984868dad4c4f7f578d8ae08f405a56711e9ac7859fee`）。只读日志显示该次启动截至 18:41:12 无 zygote/keystore2 退出，不能据此推断全部功能通过。
- 基于包含已验收四份相机二进制的 WSL 隔离快照，`:app:testStandardDebugUnitTest :app:assembleStandardDebug --rerun-tasks` 成功，47 项任务执行；701 项测试、0 失败、0 错误、3 跳过。测试数减少是删除了专用原型测试。未启用上轮有启动回归的 R8 配置。
- 测试 APK `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-beta-ringtone-clean.apk`，SHA-256 `f261d523e9e259cc277be74847080f7361ba562b09fe4fbe339bc3c81112419c`，43,632,245 字节，比基线少 4,096 字节；包名、版本与调试证书保持一致。对比基线，两包非签名 ZIP 条目名称不变，内容仅 `classes4.dex` 不同，其他 DEX、所有资产、原生库和资源逐项相同；新 DEX 不含原型名字。对两版 `classes4.dex` 用 `dexdump -d` 对类及指令归一化比较：696 类变为 692 类，恰好移除原型及三个内部类，其他 692 类的归一化结果一致。此比较依赖反汇编和归一化准确性，不证明动态反射或设备行为绝对相同。
- 尚未安装或实机验证。覆盖安装前关闭 Guest，随后测试现有实例启动、画面、铃声以及其他支持功能；回归时先停止裁剪并定位或恢复基线，不保证 APK 覆盖能撤销用户数据或运行树变化。普通工作区的四份相机二进制仍与验收快照不同。

### 实例删除桩裁剪（2026-11-15）

- 铃声原型裁剪包已通过一次基础回归。本轮范围调整为保留 `ImageStore.resetData` 及其测试，只删除 `ImageStore.delete` 和 `InstanceDeletionTest`；据此仅移除 `delete` 不可用桩及对应守卫测试，`resetData`、`InstanceResetTest`、`wipe` 和实例存储链保持不变。全源码定向搜索已无 `ImageStore.delete` 调用。
- 9 个旧图形资产因删除后原厂相机直接崩溃，已列为受保护运行资产；新包构建索引和 APK blob 均验证这 9 项存在且 SHA-1/大小一致。不得再将这些文件归入旧图形裁剪。
- 原始 3 个文件备份位于 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-disabled-instance-actions-prune\` 并附 SHA-256 清单。回滚只恢复 `ImageStore.kt` 的 `delete` 桩和 `InstanceDeletionTest.kt`，不覆盖 `resetData` 或其他未提交修改。
- 基于已通过铃声包复制新的 WSL 隔离快照 `/home/<user>/.cache/rme-wsl-build-EOnMk0DP/project-050-delete-stub-clean`，未从普通工作区覆盖相机验收二进制。`:app:testStandardDebugUnitTest :app:assembleStandardDebug --rerun-tasks` 成功，47 个任务实际执行；700 项测试、0 失败、0 错误、3 跳过，比上一包少一项删除桩测试。
- 测试 APK：`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-beta-delete-stub-clean.apk`，SHA-256 `346b0826069064b83302aa52de8a6fcfa23ed27ecba9f3ef03e0a16301d8ee1f`，43,632,245 字节。`apksigner` v2/v3 和 `zipalign -c -P 16` 通过；证书 SHA-256 仍为 `494eb713e0b14a19cb11d08e8cabb0a0b0fc25c341e57eaf0581c122527be389`。
- 与 `ringtone-clean` 比较，476 个非签名条目名称全部一致，内容仅 `classes4.dex` 不同；`engines.idx` 完全相同，其他 DEX、资产、原生库和资源逐项一致。尚未安装或实机验证，不能代替设备启动及功能回归。

### 最终停止裁剪与候选审计（2026-11-15）

- 当前最终测试包确定为 `RedMagicEMU-0.5.0-beta-delete-stub-clean.apk`，本轮裁剪到此停止；SHA-256 `346b0826069064b83302aa52de8a6fcfa23ed27ecba9f3ef03e0a16301d8ee1f`，43,632,245 字节，证书 `494eb713e0b14a19cb11d08e8cabb0a0b0fc25c341e57eaf0581c122527be389`。该结论是包选择，不扩大为未记录的兼容性验收。
- 只读候选探测（仅临时快照，不修改最终包）：Compose `ui-tooling` 移除差 116,948 字节；旧通用导入链及 Brotli/Junrar 移除差 238,676 字节；将 `material-icons-extended` 的 11,400 个图标类替换为一个占位核心图标，APK 上限差 6,720,704 字节，但图标视觉不成立，不能作为交付物。
- `libclang_rt.ubsan_standalone-aarch64-android.so` 被 `libeffects.so`、`libeffectsconfig.so`、`libkeymaster3device.so` 的 `DT_NEEDED` 引用；它不是无主文件，禁止裁剪。9 个旧图形资产继续受保护，SwiftShader 三大资产也不能仅按体积删除。R8 收缩仍需独立解释并复测上次 zygote/SIG 137 回归。
- 本轮没有继续修改源码、依赖、资产或 APK；最终包、实例和原始固件保持不动。以后重新开始裁剪时，应另建阶段任务，不以本次只读探测结果直接替换当前包。

### 未接线离线实验清理（2026-11-15）

- 本轮一次清理多项未接线实验：移除 `native/gueststubs/aod-register/` 的 3 个符号查找原型源码/测试、`native/gueststubs/aod-loader/` 的 5 个独立加载器原型源码/测试及 `native/gueststubs/radio-integration/` 的 8 个无线电 VINTF/手工 canary 原型源码/测试。三组均为未接入 Guest 的离线实验，未发现生产构建入口或外部专属名称引用，APK 内容扫描未发现专属符号；静态检索不能排除未知拼接调用。保留各实验说明和恢复位置，不触碰现用 AOD 代理、资源重建、相机或无线电实现。
- `aod-register` 的 3 个文件及原 README 备份于 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-aod-register-cleanup\`；另两组的 13 个源码/测试和 AOD 原 README 备份于同目录下 `before-offline-prototypes-cleanup\`。均逐项校验 SHA-256，并附清单。AOD 组原为未跟踪工作；无线电原型为已跟踪文件。恢复时按清单原路径复制对应备份，恢复无线电原型后移除本轮新建的归档 README；不重置其他工作。这些清理只整理源码，不缩小 APK。
- 另精确删除 `native/` 和 `tools/` 下 18 个被 Git 忽略且有对应 `.py` 源文件的 Python 字节码缓存，合计 184,954 字节；它们可由 Python 重新生成，没有删除源码、许可证或目录。
- 设备只读检查确认已安装上一版 APK，回退后该次启动日志截至 18:37:42 未再记录 zygote 或 Keystore2 退出；这不代表所有功能均已验证。本轮没有更改、构建、安装或运行 APK，也未宣称上一轮 Keystore 事故的根因已确定。

### 指定资产、旧导入链与代码收缩（已撤回，2026-11-15）

- **设备回归失败**：`0.5.0-beta-trim-r8.apk` 中 zygote 反复以 137 退出；截图同时显示多个厂商服务崩溃，`boot completed` 标记不能代表启动成功。设备现场的 Guest 崩溃记录显示 `system_server` 因无法连接 Keystore 服务抛出异常，之后 zygote 退出；Keystore 未启动或不可用的根本原因尚未确认，不能断言由某个删除项、R8 或内存压力造成。
- 已停止继续裁剪，并按此前精确备份撤回本轮 24 个资产、`Importer.kt`、构建依赖及 R8 配置；撤回前的源码与配置另存于 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\zygote-regression-before-revert\`。工作区现与前一已验收构建快照一致，既有四份相机资产差异仍按先前记录处理。恢复 APK 为同一检查点目录下 `RedMagicEMU-0.5.0-beta-graphics-pruned.apk`，SHA-256 `070f18d8c8c5de9b164984868dad4c4f7f578d8ae08f405a56711e9ac7859fee`。设备只读核对现装 APK 哈希与该恢复包一致；回滚后日志显示 18:34:13 Keystore2 启动、18:34:22 zygote 启动、18:34:45 系统报告启动完成，截至 18:35:41 未见 zygote 再次退出。此记录不能代表全部功能已回归通过。重新启动时旧版会尝试修复运行树，但不能保证恢复用户数据或所有已修改文件；未经明确确认不要清空数据、重导入或重置实例。
- 运行层核对结果。API 36 的 `ownVendorUnderGsi=true`，实际 API 36 两个 vendor 条件下均加载 own 图层，比较两个 APK 的最终层名称及内容哈希一致；不能将 14 个基础 vendor 资产被删除直接认定为本次崩溃根因。其余路径、运行树实际状态和 R8 动态行为仍需证据。

- 本轮原处理：4 个旧 KK GLES 资产、5 个旧 common 图形/copybit/overlay 资产、`kk/dhd.props`、14 个被 own-vendor 覆盖的基础 vendor 资产，共 24 项；原件逐项备份及 SHA-256 清单位于 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-asset-prune-24\`，现已撤回。旧构建脚本可能重新生成部分资产。
- 删除 `Importer.kt` 中未被指定 ZIP 公开入口调用的旧通用格式私有处理链，并同步移除仅该路径直接使用的 Brotli、Junrar 依赖。指定 ZIP 校验、`TargetPackageTree`、ramdisk、APEX 所需 Commons Compress/XZ、指纹校验和既有版权许可保留。修改前 `Importer.kt` 与构建脚本备份于 `before-target-import-source-prune\`；恢复时仅按备份恢复相关内容，不得重置其他未提交工作。旧通用格式不再受支持。
- debug 测试包启用 R8 代码收缩，`app/r8-debug.pro` 禁用混淆和优化并保留 `app.aemu`、`app.redmagicemu`、`dev.lk` 下应用类与成员，以保护 JNI/反射入口；第三方反射和动态调用仍需设备回归。为解决 Commons Compress 对 Zstandard 可选类的构建缺失，加入 `com.github.luben:zstd-jni:1.5.7-3` 和 `org.slf4j:slf4j-nop:1.7.36`，没有屏蔽缺类警告。[zstd-jni](https://github.com/luben/zstd-jni/blob/v1.5.7-3/LICENSE)、[Zstandard](https://github.com/facebook/zstd/blob/v1.5.7/LICENSE) 和 [SLF4J](https://github.com/qos-ch/slf4j/blob/v_1.7.36/LICENSE.txt) 的原始许可证文本包含在 `app/src/main/res/raw/` 和 APK；新增依赖的 Windows/macOS 二进制资源仍保留在 APK 中。release 构建配置未启用此 debug 收缩设置。
- WSL 集成构建成功；初次完整单元测试执行 713 项、0 失败、3 跳过。收缩包构建后，在隔离构建快照追加仅供本地使用的测试，使用原始 `super.img` 与 `ramdisk.img` 的只读输入，在 Linux 文件系统内创建并随后安全清理临时运行树；全套测试 714 项、0 失败、0 错误、3 跳过，新增真实固件分区/ramdisk 解析测试通过。该测试验证 `TargetPackageTree.extract` 和系统文件存在，不等同在 Android 上从 ZIP 完成新建实例导入；应用代码收缩及第三方动态调用也不能由 JVM 单测证明等价，仍需设备测试。
- APK `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-beta-trim-r8.apk`，SHA-256 `940700b7e0d1059d29188f07d3ba29b0622392c99dd6d355f97f01983535b99c`，29,445,643 字节，比上一 APK 少 14,190,698 字节。经 zipalign 处理及旧调试密钥重新签名，包名 `app.redmagicemu`、`0.5.0-beta`、versionCode 8 和证书保持一致。APK 对比：恰好删除 24 个预期资产索引，其他索引一致；7 个原生库与 3 个 Guest 相机资产逐字节不变；新增许可文件已打包。DEX 压缩由 21,148,401 变为 5,480,056 字节，DEX 与其他代码内容并非相同。
- 交付时本包尚未安装；随后实机测试发生上述 zygote/Keystore 回归，本轮已撤回，该包不能作为当前可用包。覆盖旧 APK 不能保证撤销运行树已发生的改变。普通工作区的四份相机二进制仍与验收快照不同，必须使用隔离快照构建以保持相机验收文件。

### 图形裁剪包之后的候选分析（历史记录，2026-11-15）

- 设备验收记录：上方 APK 完成一次基础回归，未逐项列明范围。本次以 `070f18d8c8c5de9b164984868dad4c4f7f578d8ae08f405a56711e9ac7859fee` 测试包及当前源码为基准进行静态审计，未删除文件；APK 索引/压缩条目估算不代表实际裁剪重建后收益，也不能排除动态拼接与旧运行树状态。
- 候选：`engines/kk/libGLES.so`、`libGLES_aemu.so`、`libGLES_route.so`、`libGLES_split.so` 独占 blob 压缩合计 224,656 字节。N64 的调用受 GPU 设置及运行树 `system/bin/app_process32` 存在与否控制；指定原始固件缺少该程序，32 位翻译器已删，但旧实例可能不同。需检查预编译和历史运行树、备份并验证已有实例图形。
- 候选：`engines/common/libhEGL.aemu.so`、`libhGLESv1_CM.aemu.so`、`libhgl.aemu.so`、`copybit.default.so`、`overlay.default.so` 独占 blob 压缩合计 54,517 字节，主要供旧系统安装路径使用；`libhgl` 还有其他旧代码路径，动态调用与历史状态尚需核对。`engines/kk/dhd.props` 独占压缩 6,143 字节，此前未查到直接读取，但不能排除动态引用。
- **2026-11-15 实机结论：以上两组共 9 个图形资产禁止裁剪。** 删除后原厂相机直接崩溃、无法打开。上述“候选”评估作废；9 个文件均须保留在 APK 来源中，包含 4 个 `engines/kk` GLES 文件和 5 个 `engines/common` 旧 EGL、copybit、overlay 文件。后续包与集成检查应确认它们仍存在，不得再以“旧系统路径”或低压缩收益为由删除。
- `n64-36` 基础层 14 个同名 vendor 文件可能被 own-vendor 层覆盖，独占 blob 压缩合计 183,512 字节；覆盖依赖 `vendor/build.prop` 和固件/旧运行树状态，不能直接认定可删。
- 固定 ZIP 专用入口以 `TargetArchive`、`TargetPackageTree` 为主；`Importer.kt` 中旧通用格式的私有递归调用图仍随源码编译，可作为一组进一步检查的候选。Brotli 和 Junrar 直接调用仅见于该旧路径；移除应先核对反射及测试并同步调整依赖，保留来源许可，再验证真实目标 ZIP 导入。`TargetArchive` 仍使用 Commons Compress ZIP，`BootImage` 仍使用 XZ，不能把这两项一起删。实际 APK 收益需重建测量。
- 当前 APK 的 DEX 压缩 21,148,401 字节，包含约 11,400 个 Material 图标类；源码引用约 53 种不同图标。该数不是可节约字节。上轮仅移除调试预览工具的隔离对照包相差 527,107 字节（含打包布局变化），DEX 压缩差 109,950 字节，且会移除预览 Activity；R8 收缩对照仍因缺失 Zstandard 可选类构建失败。均未证明设备行为等价。
- 目标 SwiftShader 及 GLES1 动态加载链仍有使用或回退路径；目标相机、振动、音频资产及 `aemu_true.so` 等不得凭名字删除。普通工作区四份相机二进制仍与验收 APK 不一致，后续构建仍须使用验收快照或先完成可重建接入。

### 0.5.0-beta 验证后的候选分析（历史记录，2026-11-15）

- 设备验收记录：上方批量裁剪包完成一次基础回归，未提供逐项测试范围。本次审计以该 APK（SHA-256 `d2306cc0b37c80713e515f9999ae3d301f2964265704f8b2eda824f96438e848`）和当前源码为基准，未裁剪产品文件；构建对照仅在 WSL 独立快照中执行。APK 的引擎 blob、DEX 和原生库压缩量分别为 31,933,016、21,151,596 和 5,659,121 字节。以下仅为候选和估算，实际 APK 差值需对照构建，静态检查也不能排除拼接式动态加载或 Guest 运行树中的引用。
- `engines/n64-gl/system#lib64#egl#libGLESv2_swiftshader.so` 独占 blob 压缩 6,146,949 字节。API 36 的 `n64-36` 图层有同名文件，`TreeFixer` 后层覆盖前层；两份文件内容并非完全相同，必须保留验收包中 API 36 实际使用的版本。前者还是旧引擎构建脚本的输入，宜先评估仅排除 APK 打包，保留来源、许可证和构建文件；实际验证目标新旧实例、显示和相机。
- `n64-36` 图层的三个 `system#lib#egl#` 32 位 SwiftShader 为独占 blob，压缩合计 4,509,941 字节；该层另有五个 `vendor#lib#` 32 位 ELF 独占压缩合计 96,269 字节。此前 `libqemu_arm.so` 已移除，32 位执行链因此不完整，但不能单凭文件名证明其他依赖不存在。删除这些资产会改变 `TreeFixer` 的 shipped 集合，可能使旧实例恢复停放的原文件；需验证新实例、旧运行树恢复及图形、相机。own 层还有两个 32 位库，可能作为固件缺文件时的备用，暂不合并断言可删；全 APK 的 29 个 ELF32 blob 不能整体删除，某些如 `aemu_true.so` 是运行时识别的特殊标记。
- `libglserverd_kk.so` 与 `libglbuf.so` 压缩合计 70,488 字节；目标 ARM64 的 `n64Has32` 因翻译器已删而为 false，旧或非 ARM64 元数据分支仍引用。已检查本 APK 13 个原生库的 `DT_NEEDED`，未发现其他库静态依赖该二者；仍须核对动态调用和旧实例。`libaemum4v.so` 与 `engines/common/libOmxVidEnc.aemu.so` 共压缩 89,560 字节，宿主 socket 仍启动，发现的 Guest 组件安装在旧系统分支；是否还有未知消费者需核查，裁剪需回归录像与视频。
- 图标依赖提供约 11,400 个 Material 图标类，而源码引用约 53 种不同图标。APK 的全部 DEX 压缩共 21,151,596 字节，这不是可节省量。隔离 WSL 快照试启用 R8，关闭混淆与优化，但构建因 Commons Compress 引用缺失类 `com.github.luben.zstd.ZstdInputStream` 失败，没有可靠的缩包数据；不能通过吞掉警告来伪称通过，也不能据此断定现有导入有问题。后续需厘清可选依赖及格式支持、JNI/反射保留规则，再对照构建和实机回归。
- 独立快照中仅移除 debug 的 Compose `ui-tooling` 并保持 R8 关闭，两份对照 APK 分别为 60,273,478 和 59,746,371 字节，相差 527,107 字节，其中 DEX 压缩量减少 109,950 字节；包布局变化也影响总差额，不能将差额全归因于该依赖。预览 Activity 随之移除，影响 IDE 的运行预览；该包未安装或回归，正式工作区和验证包未改动。
- `libpayloaddumper.so` 和 `libsevenzip.so` 分别压缩 3,729,443、1,620,429 字节，当前导入代码仍使用；现有内置回退的格式范围和性能不完全等价，尤其 RAR5 需要 7-Zip。要评估必须用需要支持的固件实测导入结果、哈希及耗时，不能按未使用组件删除。未接线 AOD JNI 原型可清理源码但不影响 APK，AOD 资源重建工具仍被使用。
- 当前工作区四份相机图形/HAL 二进制与验收 APK 均不同；要稳定复现通过测试的相机，仍需处理可重建来源及工作区接入。此项是工程可复现性问题，本次未修改。

### 剩余裁剪汇总（只读审计，历史记录，2026-11-15）

- 审计基准为上方 `old-engines-pruned` APK（SHA-256 `2b33778e054026f959869ebd092e80f2e1f58756b6b58069e01a43794698d395`）和当前源码；该 APK 尚无设备回归记录。APK 内含 48 个 arm64 宿主原生库，压缩合计 8,190,896 字节；引擎索引有 1,620 条、395 个去重 blob。本节是静态候选清单，不代表删除已通过构建或实机验证，也不能排除动态拼接名称或其他外部状态。
- **可优先进入专项验证**：`libdhdslot.so`（压缩 35,460 字节）和 `libglbridge.so`（68,799 字节）分别由 GB 服务与 KK 内置图形桥路径使用；正常目标走 N64。前者涉及 Manifest 中的非导出服务、JNI 动态加载；后者的 N64 图形由另一套 `GlHost` 路径提供，但同名源码目录并非整体无用。APK 所有原生库的原始名称扫描未发现额外消费者，`DT_NEEDED` 检查针对工作区库，不等同逐个检查测试 APK 内的 ELF。删除前须继续核对动态调用、同步处理入口并实际回归。
- **小型旧资源候选**：`engines/kk/` 下 `aemu-run013.jar`、`audio.primary.mtk.so`、`audio.primary.default.so`、`audio.primary.ics-qcom.so`、`audio.primary.ics.so`、`audio.primary.jb41.so`、`gralloc.abi.so`、`gralloc.default.so`、`libashmemshim.so` 共 9 条，各自独占 blob，压缩合计 24,893 字节；其旧 API、32 位或 KK 路径仍在源码中，须逐项验证。`engines/common/librecshim.so` 为禁用的 Recovery 路径使用，独占压缩 2,707 字节，未来恢复 Recovery 需恢复它。`engines/kk/dhd.props`（压缩 6,143 字节）尚须确认是否存在二进制或动态使用，不能视为已证实安全。
- **较大潜在收益但当前不能直接裁剪**：非 36 的 N64 目录共 1,397 个索引项，涉及 347 个 blob；其中仅该组拥有的 177 个 blob 压缩共 11,796,423 字节（约 11.25 MiB），其余 170 个与保留目录共享，不能重复计算。`TreeFixer` 在目标 API 36 上仍枚举历史资产名称，用于识别并恢复旧运行树；需要先设计可验证的历史名称保留及旧实例恢复方案。15 个非 36 版本的 30 个 runner/Binder 压缩合计 753,265 字节，实例元数据缺失或旧值仍可能选择它们，直接移除可能改变启动行为。以上为 APK 条目压缩量而非保证节省，实际差额需重建对比。
- **实验振动候选**：`libaemu-vibration-launch.so` 与 `libaemu64_vibration_36.so` 共压缩 57,112 字节，实验开关当前关闭；是否舍弃该实验链需核查其完整调用与可重建来源，并回归现有振动。已启用的直接振动链和 `libaemuvibration.so` 不属于此候选。
- **按名称不能直接删除**：`libqemu_arm.so`（压缩 1,617,139 字节）仍可能翻译 Guest 的 32 位原生程序，32 位 APK 无法安装不足以证明它无用；`libglserverd_kk.so`、`libglbuf.so` 在 N64 兼容路径有引用。`engines/kk/aemu-stubs.jar`、旧实例音频策略恢复文件、目标 N64 和 RedMagic 资产均有实际调用。`libpayloaddumper.so`、`libsevenzip.so`、`libpdgwrap.so` 合计压缩 5,352,384 字节，但被现有固件导入功能调用；移除属于改变输入格式支持，不是无用代码裁剪。
- **源码清理候选，收益很小**：旧更新偏好访问方法及未引用字符串、无入口的 Recovery 设置界面、未接入输入链的 `SessionInputGate` 原型，以及不进入 APK 的独立相机 `camera_read` 原型与测试。它们仍需复核引用和保护未提交文件；相机图形桥诊断构建器、检查点实验源码、已接入 AOD 重建工具和振动/Wi-Fi 运行链应保留。不要删除设备中既有偏好数据或相机验收所需二进制。
- 本次没有删除组件、构建、安装或实机测试。要执行任何候选，应先用相机验收快照建立检查点，再分组验证；直接从普通工作区构建仍不能重现相机验收所用的四份二进制。

### 后续裁剪只读审计（历史记录，2026-11-15）

后续裁剪候选从以下静态证据开始。本次未删除任何代码或资产；以下仅为源码、APK 和预编译文件的静态证据，尚未经过实际裁剪、构建或设备回归，也不能排除未知的动态拼接引用。

1. **旧多实例界面**：`Home.kt` 中的 `MachineCard`、`HomeFab`、`LibrarySheet` 及私有配色函数仅见于该文件；现有主界面使用 `MainActivity` 中的单实例 `Library`。源码及预编译文件定向扫描未发现其他引用。可作为单独候选先核查并备份，保留共享实例模型、配置、资源与来源；之后回归导入、启动、设置和触摸。实际 APK 收益尚未测量。
2. **旧 GB 引擎二进制**：`app/src/main/jniLibs/arm64-v8a/libqemu_gb.so` 在当前 APK 压缩后 1,668,748 字节。其源码名称对应 `Engine.GB.qemu`；目标生产启动经 `GuestImage.effective()` 选择 N64。已检查 55 个打包 JNI ELF 的 `DT_NEEDED`/`SONAME`，未发现对该名称的引用；资产、原生文件、压缩档及 APK 的定向字符串扫描也未发现额外消费者。旧 GB 路径仍存在，现有测试只覆盖旧 KK 元数据，未覆盖 GB 元数据，上述扫描也不能排除运行时拼接或未扫描外部组件。实际裁剪前须补 GB 元数据的选择测试，备份原件并回归新旧实例、32 位应用和图形。不能连带删除 N64 路径仍可能使用的 `libqemu_arm.so` 或 KK 图形服务；保留来源和许可。
3. **旧系统相机 HAL 资产**：`engines/gb/libcamera.so`、`engines/common/libcamera.semc16.so`、`libcamera.hc.so`、`libcamera.none.so` 的读取在旧系统分支；正确的 N64/ARM64 目标路径先返回。四个 blob 在当前 APK 分别只有一个索引引用，压缩合计约 16.7 KiB，不含索引及 ZIP 开销；构建脚本仍生成这些资产。目标元数据异常、未知动态引用和运行结果尚未完整验证；不能删除相机源码、脚本、来源许可或 N64/目标相机资产。
4. **未接线 AOD 画布原型**：`TargetAodCanvasRenderer.kt`、`TargetAodCanvasPlan.kt`、`TargetAodGeometry.kt` 及其两个测试只形成自包含原型；Renderer 明确标为未接线，未发现其他生产调用。这五个文件是既有未跟踪工作，删除前必须备份，且不可连带删除已接入的 `TargetAodImportStage`、`TargetAodMaskResource` 或 AOD 代理资产。仍需单独验证主题、息屏和唤醒；尚未测量 APK 收益。

另有仅供已拒绝的 Recovery 路径读取的 `engines/common/librecshim.so`，压缩约 2.6 KiB；若未来恢复 Recovery，需同时恢复该资产和兼容链。共用 `libaemushim.so` 不属于此候选。小体积项的收益可能不足以抵消验证成本。

### 原始裁剪候选审计（历史记录）

本次只读审计，以下均不是已证明可删除的结论；每次实际裁剪仍须单独检查依赖、构建和回归，并保留版权、许可和来源。

1. `app/src/main/assets/engines/hc/HTJ85B-140714/` 四个 Honeycomb 图形库：[HcHwRender](../app/src/main/java/app/aemu/core/HcHwRender.kt) 在 API 不为 11–13 时立即返回，[TreeFixer](../app/src/main/java/app/aemu/core/TreeFixer.kt) 传入 Guest API；指定 NX809J 为 API 36。定向源码与预编译文件扫描未发现其他明确引用。四个源文件共 1,134,198 字节，按已有 APK 中仅被该目录引用的去重 blob 压缩大小估算可省约 0.514 MiB；未计索引和 ZIP 开销，未实际删除重建。后续须验证索引、启动、显示、触摸及已有实例兼容性。
2. `InstanceStorageOptions.kt` 与对应测试：当前源码仅测试引用该辅助对象，原容量滑块已移除；静态扫描未发现其他引用。它们是既有未跟踪文件，须独立处理；仍在使用的 `diskMb` 元数据及相关运行路径不能据此一起删除。
3. `AppUpdateManager.kt` 与 `UpdateDialog.kt`：旧更新入口此前已移除，当前发现模块内部相互引用及 Activity 中残留导入。须进一步核实反射、动态调用和模块内依赖，再成组验证；不能顺带删除其他功能共用的 provider、安装权限或来源许可。

不可按目录名直接裁剪其他 `n64-*`：`TreeFixer` 仍枚举其他引擎索引用于处理历史运行树；目标 Guest 的 `n64-gl`、`n64-36`、`n64-36-own` 还是实际图层。目标是 ARM64 也不能单凭这一点删除 32 位程序可能使用的翻译器。引擎资产按内容去重，源目录大小不是实际 APK 节省量。

## 运行基准

- 2026-11-15 验收的新实例相机实验包 SHA-256：`cc986cd19d6a7dc89d323e943533fada7b01cd2a7ed67d71fa35dc300709f132`。备份：`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\camera-lux-diagnostic.apk`。安装前原标准包也保存在同目录。备份位于 Git 外，不含原始固件或应用数据。
- 该包是实验版，相机图形桥的断线、旧帧和资源生命周期限制尚未闭环。实验源码和二进制已备份；当前工作区的正式资产与实验包不同。功能证据和限制见 [兼容矩阵](COMPATIBILITY.md)、[进度](PROGRESS.md)及[图形实验说明](../tools/diagnostics/oes-fbo-prototype/README.md)。
- 以工作区为基础的隔离构建目录位于检查点目录 `build-source`，其中仅将实验包中对应的宿主图形库、Guest 图形库和两份相机 HAL 换为已验收实验二进制。基线重建包为 `reconstructed-baseline.apk`，与实验包签名证书一致；除 `classes7.dex` 外，非签名 APK 条目内容一致。差异未完成归因时不得声称两包运行行为完全相同。实验资产尚非正式工作区接入，不能从普通工作区直接构建出该实验包。
- Windows 基线测试 `:app:testStandardDebugUnitTest`：727 项，1 项失败，90 项跳过；`InstanceRuntimeLeaseTest.anotherProcessBlocksAndDeathReleasesLock` 完整测试中两次失败，单独重跑一次通过。其产品类当前标为实验性且未接入 VM 启动；失败原因尚未证实，不应作为回归通过。

## 本轮回归清单

以下以现有支持范围为准；每项必须记录结果，旧实测不代替新 APK 的验证。基线限制、已排除场景和适用条件仍以兼容矩阵为准。

- [ ] 覆盖安装同一标准包，保留实例；启动应用和现有 NX809J Guest，解锁及正常关机、重启。
- [ ] 应用主界面、设置页与 Guest 侧菜单正常显示；检查浅色、深色主题及返回操作。
- [ ] 触摸、键盘输入和基本显示；主题、息屏画面与唤醒动画。
- [ ] 虚拟 Wi-Fi 或以太网连接、DNS 和网页访问；虚拟短信、来电与飞行模式往返。
- [ ] 音频播放、录音回放与振动；加速度计响应和已支持的刷新率设置。
- [ ] 共享文件、Guest 应用安装及重启后数据；WebView 和视频播放。
- [ ] 原厂相机前后摄切换、普通拍照保存、图库/DCIM 查看、浏览器调用相机。权限拒绝不在本轮范围；断连、占用恢复、长时间和其他宿主仍未验收。

## 迁移记录

### 宿主主题函数（2026-11-15，设备回归待补）

- 将宿主 Compose 函数 `AemuTheme` 改为 `RedMagicEmuTheme`，同步修改主界面、设置页和 Guest 菜单的三个直接调用；没有改变主题实现、协议或 Guest 启动流程。源码调用点、JNI、库加载、动态查找、Guest 辅助类、设备及通信路径、资产索引、预编译文件和压缩 JAR 均已核查；除 APK 中的宿主 DEX 外未发现该名字。静态搜索不能排除未知的动态拼接引用。网站另有 `aemu-theme` 存储键，不属于本项。
- 编译探针在修改前因新名称不存在而失败；改名后隔离构建成功。最终 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 成功：727 项、0 失败、0 错误、90 跳过。此前跨进程锁测试在完整基线测试失败而单项通过，最终整套测试通过并不能消除其不稳定性。
- 测试 APK：`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-theme-test.apk`，SHA-256 `ce0cda08992eed78bb74969c3e9d426ff277f269d786155622a0beb78c462d16`；applicationId `app.redmagicemu`，调试证书与相机实验包一致，版本显示仍为 `0.4.0-beta`。与隔离重建基线及相机实验包比较，非签名 APK 条目均仅 `classes7.dex` 不同；实验包与隔离基线本身已有 Compose 指令差异，未证明语义等价。相机实验库仍有上述限制。
- 设备验收记录：该包完成一次基础回归，但未逐项说明安装、启动及回归步骤，故上方各项不据此勾选。发生回归先停止迁移并定位或恢复检查点，不继续裁剪。回滚源码只把 `Theme.kt` 的声明及三个调用恢复旧名；恢复相机实验包前正常关闭 Guest，再覆盖安装同目录 `camera-lux-diagnostic.apk`，保留实例和原固件。不得重置其他未提交更改。

### 启动准入函数的内部包名（2026-11-15，设备回归待补）

- 阶段分支 `codex/0.5.0-system-boot-namespace`。将 `SystemBootOnly.kt` 从 `app.aemu.core` 移至 `app.redmagicemu.core`，同步 `GuestVm` 和两项现有测试的导入；函数逻辑、系统启动与 Recovery 拒绝条件和错误信息不变。迁移前现有两项测试通过；先在隔离构建中引用新包，编译按预期因新包尚不存在失败，随后完成迁移。
- 核查了源码、JNI、库加载及动态查找、反射、Guest 辅助类、设备/通信路径、资产索引和预编译二进制；递归扫描资产及压缩档，包括上轮测试 APK 和相机实验包，仅在宿主 DEX 中发现旧符号，未发现 Guest 或预编译组件引用。静态扫描不能证明不存在未知的动态拼接引用。
- 隔离构建首次完整测试为 727 项、1 项失败、90 项跳过；失败仍是上轮基线已有的不稳定跨进程锁测试。重跑 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 成功，727 项、0 失败、0 错误、90 项跳过，其中两项启动准入测试均通过。不将重跑通过解释为锁测试问题已经修复。
- 测试 APK：`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-system-boot-test.apk`，SHA-256 `5711dbd1bf04ca4487d8893b8e85f90a1aeadc636b0161b07f1bd89b9b5d4f93`。与上轮 APK 相比，非签名 APK 内容仅 `classes4.dex`、`classes10.dex` 不同；相机实验库、`app.redmagicemu` 和调试签名证书保持一致。版本显示仍为 `0.4.0-beta`。
- 安装后记录到正常启动；未逐项说明其他回归场景，故不据此勾选其余项目。回滚源码仅将 `SystemBootOnly.kt` 移回原包并移除 `GuestVm` 与测试中本轮新增的导入；需要恢复 APK 时正常停止 Guest，覆盖安装同目录 `RedMagicEMU-0.5.0-theme-test.apk`。保留原固件、实例以及其他未提交工作。
