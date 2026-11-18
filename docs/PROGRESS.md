# 开发进度

按阶段记录功能需求、实现及验证结果。目标 Guest 为指定的 REDMAGIC NX809J 原厂 Android 16 / API 36 / ARM64 固件。系统功能验收见[兼容矩阵](COMPATIBILITY.md)。

## 0.6.2-beta GPU 阶段概览（2026-11-18）

| 需求 | 实现 | 验证 |
| --- | --- | --- |
| 常规 APK 启动 Guest 时自动接入 Vulkan | VM 启动宿主渲染服务，并在 Guest Android 启动前安装 Vulkan 客户端。Guest 应用经 gfxstream 将命令提交到宿主 Adreno。 | Adreno 750 的 Geekbench 6.7.1 Vulkan 调优包实测 **15,263**，8 项有效。 |
| Guest 应用使用宿主原生 OpenCL 和 FP16 | Guest 的公开 OpenCL 库经实例私有 socket 调用宿主厂商驱动。FP16 扩展与能力值取自原生驱动。 | Adreno 750 的 N64 FP16 内核校验通过。同一手机的原生 Geekbench 6.7.1 OpenCL 对照为 **14,535**。 |
| 减少 OpenCL 传输中的等待和拷贝 | 线性缓冲区读回接入异步完成和共享槽。线性缓冲区、图像及矩形缓冲区的非阻塞上传由宿主保留独立暂存，完成后回收。 | 矩形上传包的 OpenCL 首轮 **11,128**，复测 **12,326**，两轮各 8 项有效。上一版图像上传包复测 **12,777**。图像和矩形缓冲区的异步上传均在 Adreno 上通过回归。 |

## S0 基线审计（2026-11-04）

- 项目从 AEmulator 上游 `main` 检出并建立独立基线，初始审计使用 `s0-baseline-audit` 分支。
- 代码入口、构建要求、预编译资产及风险见 [BASELINE.md](BASELINE.md)。本轮没有修改产品运行逻辑。
- 后续提供的旧版 AE 导出日志已作脱敏审阅，事实与假设见基线。指定固件未提供，固件身份和 HAL 因果未验证；旧日志属于早期测试版。构建和真机验证状态以基线记录为准。
- 使用本机 JDK 21 和新配置的 Android SDK，`assembleStandardDebug` 构建成功；原生组件独立重编译、安装和真机运行未验证。S0 审计已记录可用证据及限制。
- 进入 1A 后以最小复现验证改动。固件校验依赖可信身份信息与可访问的目标样本。

## 0.1.0 任务 1A：品牌和安装身份（2026-11-04）

- 在 `1a-brand-identity` 阶段分支实施；保留原有未提交的基线文件和 `.gitignore` 修改。标准版本地开发默认 applicationId 为 `app.redmagicemu`，clone 和 v0003 分别为 `app.redmagicemu.clone`、`app.redmagicemu.v0003`。namespace、JNI、原生库及 Guest 通信协议未迁移。
- 用户可见名称、关于、目标固件说明、通知相关名称、导出文件名及 README 已调整；关于页保留 AEmulator、SENSES 来源并标明非官方。旧翻译 README 改为过时提示；部分应用语言的新说明回退英文。使用原创矢量占位图标。移除界面和启动流程中的上游更新与反馈渠道入口；未调用的旧更新实现仍保留源码。
- provider authority 和 BOOT action 按各 applicationId 派生；调试 SHELL 和 Keeper STOP 同样派生。源码检查显示 provider 调用使用运行时包名，服务使用显式 Intent；未发现快捷方式 API，旧 BOOT 自动化需重建。构建前检查复现旧 `app.aemu`、旧 BOOT 和旧品牌；修改后核对三个变体合并清单中的 package、BOOT 和 provider。
- 新 ID 是独立应用，不能覆盖旧应用，私有镜像、设置和授权不会自动迁移；旧快捷方式和外部自动化不兼容。新共享目录为 `RedMagicEMU`，固件扫描目录为 `RedMagicEMU/firmware`；旧 `AEmulator` 目录不移动或删除，所需共享文件须自行复制。没有实施私有数据迁移，也没有卸载旧应用。
- 使用基线 JDK 21 和 SDK 执行 `:app:testStandardDebugUnitTest :app:assembleStandardDebug`，成功；现有 `RomMatrixTest` 3 项通过。另生成并检查 standard、clone、v0003 调试版合并 Manifest。APK 的包名经 aapt 确认为 `app.redmagicemu`。构建保留 SDK XML、Manifest 和 Kotlin 弃用等既有警告。本次构建使用现有原生资产。
- 未验证：设备安装、与旧应用并存、权限、快捷方式、provider、后台服务、导入、图标显示及 Guest 实机运行；固件身份校验尚未实现。无发布或签名发布构建。
- 回滚：在确认保留本轮以外未提交改动后，按本轮变更文件逐项恢复至 `c3e9427`，并手动移除本轮新增的占位图标；不要用全工作树重置，勿删除旧应用或任何数据。恢复身份后，新旧 ID 的私有数据仍各自独立，不会自动互相迁移。

### 任务 1A 补充：多语言 README（2026-11-04）

- 将 17 份过时提示替换为与当前主 README 对应的完整译文，并在主 README 添加语言入口。保留项目来源、作者及许可证，说明目标固件、尚未实机验证、构建要求和数据迁移边界。
- 文档检查：17 份均包含核心标识与构建命令，本地链接可解析，未发现旧发布或论坛入口；`git diff --check` 通过。翻译未经母语者审校。本次仅改文档，未重新构建或安装。

### 任务 1A 补充：ADB 实机检查（2026-11-04）

- 安装后通过无线 ADB 检查：宿主 Android 17/API 37、ARM64；已安装 `app.redmagicemu` 调试版，APK SHA-256 与本轮本地构建一致。旧 `app.aemu` 和新应用同时存在；未卸载或清除数据。主界面与设置页可打开，设置页显示版本及非官方上游来源说明。
- 包管理器解析新 BOOT action 和 `app.redmagicemu.provider`；这只是注册检查。系统通知权限已授予。
- 点击共享文件夹授权入口可进入系统设置；临时授权后主界面提示消失，随后恢复原有未授权状态。未导入固件，未验证后台服务、Guest 运行和图标在启动器中的显示；未执行 HAL 或固件修改。

### 非目标固件的导入和启动探测（2026-11-04）

- 导入器曾添加并识别一个 Huawei ohosqssi ARM64、Android 12/API 31 系统镜像，约 5.4 GB，提示实验性。这是导入器识别结果，未验证来源、完整性或真实版本，不是指定的 NX809J 固件。
- 启动该实例：VM 进程和 Keeper 前台服务出现，切回宿主桌面后短时仍存在，再次返回可见 Guest；日志报告约 73 秒启动完成，画面显示 Guest 桌面。短暂点击与滑动未观察到明确响应，无法确认触控或应用运行；长期后台稳定性未测。
- 日志记录多项 Guest 服务退出和重试，并记录运行副本中替换崩溃的音频策略、保留原件；未证明这些报错的根因或对指定固件的影响。Guest 运行期间出现外部域名解析活动。原始包未被本次测试主动修改，运行副本已由启动流程修改；未保存或提交原始日志。
- Guest 状态栏显示 AEmulator，现场记录标注为虚拟基带显示；来源待核实，未据此认定为宿主品牌残留。该非目标固件的结果不能作为 NX809J 启动或兼容性结论。测试结束时 Guest 保持运行，供后续检查。

### 非目标固件触控排查（2026-11-04）

- 对照记录显示同一宿主、同一刷机包在旧版 AE 中触控正常，本次 Guest 桌面手动触控无响应。当前设备保留旧版 `app.aemu.v0003`（0.0.0.3），本项目为 0.0.0.4 基线，未对旧版数据和设置作比较。
- 重启该运行副本复现并只读查询 Guest 输入管理器：该时刻仅列出 Virtual 键盘，没有触摸屏设备；Guest 侧 `/dev/input/event0` 存在，但当前显示为普通文件。宿主输入日志有一个 device -1 的连接；Android 输入管理器仍未列出该设备。当前 1A diff 未修改 GuestScreen、InputService 和相应原生输入代码。
- 同次运行记录 zygote 退出码 137 后重启；与触控无响应是否有关尚未确定。未修复、未修改引擎，未核实根因；原始诊断日志未纳入仓库。

### 输入连接超时修复（2026-11-04）

- 根因：旧式 Guest 输入客户端不发送设备号，宿主握手读取等待 500 ms 后超时；旧代码只在成功读取时恢复 socket 超时，后续空闲读取再次超时即关闭连接。Guest 日志显示触摸设备先被添加、随后读取到 EOF 并移除。API 31 引擎确实使用无首字节的连接方式。
- 修改 `InputService` 和 `InputHandshake`，确保握手结束时恢复无超时读取，保留旧协议的设备号 -1 回退。新增真实本地 socket 测试：旧逻辑的超时测试先失败，修复后超时及显式设备号测试通过；标准调试单元测试共 5 项通过，APK 构建成功。未更改原生引擎或固件原包。
- 通过应用菜单关闭 Guest 后覆盖安装调试 APK，保留已导入数据；同一 API 31 测试实例启动后，Guest 输入管理器持续列出 `dhd-touch` 触摸设备。ADB 上滑完成解锁、点击设置后 Guest 设置应用获得焦点。旧版应用未卸载。
- 宿主显示 16 KB ELF 对齐兼容警告；本轮仅关闭提示，未更改其策略或修复原生库。之前 zygote 退出情况未在本轮专项处理。
- 回滚：恢复 `InputService.kt` 本轮握手调用，移除新增 `InputHandshake.kt` 及对应测试，再重新构建；不重置其余未提交改动或删除数据。

### 任务 1A 补充：设置页图标遗漏（2026-11-04）

- 回归发现设置页仍显示上游 AE 图标。原因是 AboutCard 单独引用 `drawable/logo`，此前只替换了启动器图标；现改为使用现有原创占位矢量和同一背景色。旧来源资产未删除。
- `:app:assembleStandardDebug` 构建成功，通过 ADB 覆盖安装并实机打开设置页，确认显示新的占位图标；未卸载应用或删除数据。回滚时仅恢复 AboutCard 的旧图标引用和本次新增的两个 import。
- 宿主仍显示 16 KB 兼容警告，此问题未修复。对弹窗列出的四个 APK 内文件核对：它们与仓库对应预编译文件一致，ELF LOAD 标注均为 `0x4000`；其中两个是可执行 ELF。设备 `getconf PAGE_SIZE` 为 4096；`zipalign -c -P 16 4` 返回 1，项目使用压缩/提取原生库，需进一步逐项分析，不能仅凭这些结果判定警告根因或宣称兼容。未修改系统策略和原生组件。

### 16 KB 兼容提示完整列表检查（2026-11-04）

- 只读读取设备弹窗完整内容：列出 52 项，其中 8 项为“LOAD 区段未对齐”，44 项为“未知错误”。对当前标准调试 APK 的 ELF 程序头核查：51 项是 ELF64，8 项的 LOAD 对齐为 `0x1000`；另有 `libaemu32_25.so` 是 ELF32，LOAD 为 `0x1000`，不应作为 ELF64 统计。ELF64 中其余 43 项的 LOAD 对齐和偏移满足 16 KB；以上为 ELF 静态检查。
- 8 个 ELF64 的 4 KB LOAD：`libbinderd_gb.so`、`libbinderd_kk.so`、`libdhdrun_gb.so`、`libdhdrun_kk.so`、`libdhdslot.so`、`libglbridge.so`、`libglserverd_gb.so`、`libglserverd_kk.so`。按程序头对 RELRO 作静态初查，还有 8 项需进一步判断；APK 内偏移检查 `zipalign -c -P 16` 失败，但项目要求提取原生组件运行，不能单凭 ZIP 检查结果断定本弹窗的根因或直接改变打包策略。
- 设备 shell `getconf PAGE_SIZE` 返回 4096；未知错误的触发机制、真正 16 KB 页设备上的功能及长期运行均未验证。本次无代码、设备策略或弹窗设置变更。

### 16 KB 打包对齐修正与剩余阻碍（2026-11-04）

- 阶段分支 `16k-compatibility`，保留已有未提交更改。本次只从 `app/build.gradle.kts` 的资源 `noCompress` 列表移除 `so`，保留 `useLegacyPackaging=true` 和 `extractNativeLibs=true`，使原生库继续解压；未裁剪引擎、修改原生二进制或 ROM。
- 修改前 APK 的 `zipalign -c -P 16 -v 4` 失败；修改后 52 个原生条目压缩，检查 `Verification successful`。隔离构建对照：全部 448 个 assets 和原生条目解压内容 SHA-256 一致，396 个 assets 的压缩方式不变。单元测试 5 项通过，标准调试构建成功。
- 覆盖安装到原设备后，应用可启动、Guest API 31 测试系统日志记录约 40 秒启动完成，Keeper 前台服务和 dhd-touch 设备存在；未验证其他 Guest 和真正 16 KB 页设备。已安装应用包仍为 `pageSizeCompat=4`，系统弹窗仍显示 ELF 加载段对齐失败：此项尚未修复，打包检查通过不是完整 16 KB 兼容。
- 8 个 64 位旧引擎二进制仍有 4 KB LOAD；本仓库未发现可确认与全部现有二进制等价的完整源码或构建入口。`native/hostjni` 部分文件明确是替代实现，行为等价性未验证；这些旧 ELF 的段布局存在 16 KB 页重叠或偏移不同余，原生库的段布局需要在重建时处理。需要等价源码及构建依赖，或经充分验证的等价 16 KB 产物，才能在保留引擎的前提下继续；不得通过隐藏库、裁剪引擎或关闭检查冒充修复。
- 回滚打包修改：仅在 `app/build.gradle.kts` 的 `androidResources.noCompress` 列表加回 `"so"` 并重新构建、覆盖安装；这会恢复原 APK 对齐失败，且不会自动回滚设备或 Guest 私有数据。

### AE 与 RedMagicEMU 弹窗对比（2026-11-04）

- 对设备已安装 APK 只读对比：两者明确出现 4 KB LOAD 的 8 个旧引擎文件 SHA-256 逐个相同，旧版在同一设备上未出现弹窗。
- 设备中 AE `app.aemu.v0003` 版本 0.0.0.3、非调试，安装于 2026-10-19，系统登记 `pageSizeCompat=0`；RedMagicEMU 为调试版 0.0.0.4，2026-11-04 安装，系统登记 `pageSizeCompat=4`。设备弹窗称该提示面向调试应用；由于安装时间和系统记录不同，未隔离验证不弹窗的唯一原因。
- 追踪本地历史与定向公开源码：8 个旧文件在上游初始提交已经是二进制；未找到覆盖全部文件、与它们等价的构建源码。仓库 `native/hostjni/slot.c` 和 `dhdrun.c` 明示为后续替代实现；`native/glbridge` 生成的是其他桥接库，不可据名称假定等价。未替换任何原生文件，未隐藏弹窗或裁剪引擎。若要安全修复尚需对应源码/构建依赖或经验证的等价 16 KB 产物，并对 IPC 与 GPU 做设备回归。

### 0.1.0 任务 1B：固件准入（未完成，2026-11-04）

- 阶段分支 `1b-firmware-admission`。指定 VM ZIP 的摘要、大小及从派生 ramdisk 读取的元数据见 [TARGET_FIRMWARE.md](TARGET_FIRMWARE.md)。包内自述包含派生文件；bootimage 指纹与历史日志不同。未将该组合包冒充原厂原始包，正式允许摘要保持未配置。
- 已增加流式摘要校验、取消检查和私有暂存清理；缺少正式摘要时在复制之前拒绝。现有通用解包器的越界、符号链接和空间消耗边界尚未完整审计，因此即使摘要将来匹配，导入仍明确拒绝。启动和恢复在缺少可验证实例基线时全部拒绝，旧实例的标记不能放行；启动前不再刷新元数据，实例 ID 加了检查。当前所有实例均无法启动，尚无合法兼容注入的放行机制。
- JDK 21 执行 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 成功，14 项单元测试通过；覆盖摘要匹配/不匹配、未配置、取消暂存和失败清理、旧实例拒绝及 ID 格式。尚未覆盖不同构建、损坏包、错误备份和合法兼容改动的完整端到端行为；未安装或实机验证。
- 待完成：核实原厂原包身份及与日志差异；限定指定 ZIP 安全解包、资源限制和同一暂存输入的完整链路；建立可验证导入基线及兼容注入记录和验证；接通所有入口并补齐所要求的测试与设备检查。回滚本轮时仅逐项恢复本轮修改文件并移除本轮新增文件，保留原有未提交日志删除；不要重置工作树或删除实例数据。

### 任务 1B 补充：目标包确认（2026-11-04，仍未完成）

- `RedMagic_NX809J_VM.zip` 是本项目的指定原始固件输入；其中派生 ramdisk 是为 AE 读取而制作。已将该 ZIP 实测 SHA-256 配为正式目标；派生关系和 bootimage 指纹与历史日志的差异仍保留在目标文档中。
- ZIP 中央目录显示六个条目实际均为 deflate，与包内 README 所称 stored 不一致。`super.img` 展开为 19,327,352,832 字节。新增 ZIP 条目预检、暂存大小限制与暂存空间检查；该检查不覆盖 super 解压及最终树空间。安全解包与导入基线尚未完成，导入、实例启动及克隆仍拒绝。旧实例 ID、目录及重置路径增加边界检查；尚须验证 GuestVm 构造阶段读取的安全边界。
- 标准调试单元测试及构建成功：22 项测试中 20 项通过、2 项符号链接测试因 Windows 账户无创建权限跳过；未安装或实机测试。目标包实际导入、损坏数据、备份恢复、合法兼容改动及失败清理的设备验证尚未完成。暂存删除遇到 I/O 错误的结果尚未验证。
- 运行时 fixup 和 Guest 会修改多个系统目录；不能将修改后的运行树直接当成可信基线，也不能简单排除整个系统目录。重建运行副本会改变系统修改持久化行为，本轮未实施。须继续建立可验证的兼容改动模型，并完成受限解包及资源上限后才能解除拒绝。
- 指定 super 的 LP 元数据显示 `odm_a`、`product_a`、`system_a`、`system_dlkm_a`、`system_ext_a`、`vendor_a`、`vendor_dlkm_a` 有内容；`odm_a` 已识别为 EROFS，其他分区格式未全面验证。现有导入器遗漏两个 dlkm 分区，且会吞掉部分非 system 分区读取错误，不能直接视为完整可信导入。

### 任务 1B 继续：受限解析与入口检查（2026-11-04，未完成）

- 增加外层 ZIP 有界展开及 CRC、严格 ramdisk 解析、受限树写入和严格随机读取原语；这些原语尚未接入正式导入。指定 ZIP 的 ramdisk 只读解析出 28 项。LP 元数据新增摘要、表及 extent 范围检查，真实 super 前缀的校验和及 7 个线性 extent 已只读核对；EROFS 增加部分目录、名称和元数据边界检查。
- 启动准入前不再执行音频探测，准入失败后的停止操作不执行 Guest sync；内存状态只控制清理，每次启动仍重新准入。导入、启动和克隆仍保持拒绝；实例基线和兼容注入验证未实现。现有 ExtentSource 越界补零、EROFS 压缩索引及资源上限、分区完整性、链接链、取消与清理仍需完成后才能接线。
- `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 成功；Windows 单元测试 47 项中 45 项通过、2 项符号链接测试跳过。另使用 WSL 的临时 Linux JRE 独立运行同一 InstancePathSafetyTest，4 项全部通过；Android 设备仍未验证。未安装或运行真实固件。此项工作尚未达到任务 1B 完成标准。

### 任务 1B 继续：目标导入及设备验证（2026-11-04，未完成）

- 兼容处理范围仅包括 APEX 链接相对路径，以及 product/system_ext 移动后受影响链接的相对路径，不改变兼容目标。真实包静态检查发现 7 条分区链接受移动影响；Linux 对真实树迁移后完成基线及未来链接图复验。APEX 包 41 项和 OAT/引擎资产等部分合法变更已有精确校验，但并非全部运行时兼容变更。
- 指定 ZIP 现经同一私有暂存快照校验与解包，使用受限读取、分区/元数据检查、后台处理及待发布区；失败或取消清理，独占占位后完成发布。导入时签发 Android Keystore 认证基线，历史指纹差异写入导入日志。安全链接、磁盘限额、分区迁移、取消竞态、发布及强杀清理等有合成和 Linux 测试。
- 无线 ADB 可用且 Root 已完成授权；仅安装独立 `app.redmagicemu.clone`，未覆盖原应用。设备指定 ZIP 副本 SHA-256 与目标一致；最新版独立测试包完整导入生成实例 `ztv7sy` 和基线，待处理目录已清理；此前测试实例 `ndvvd6` 保留。快捷启动显示基线及前置检查通过后的“运行时兼容验证尚不支持”，Guest 未启动。设备与本机测试副本均保留供核查。
- 最新 `:app:testStandardDebugUnitTest :app:assembleStandardDebug :app:assembleCloneDebug` 成功；176 项中 43 项在 Windows 跳过，其余通过。部分链接、迁移、APEX 与发布测试已在 Linux 环境补跑。Android 完整正常启动、备份恢复、取消与进程强杀的端到端测试未完成。
- 任务 1B 仍未完成：普通及恢复启动在认证后仍统一拒绝；未覆盖所有合法运行时兼容改动，分区中断恢复尚未接入，也无法证明旧 Guest 进程已全部退出。当前同 UID 进程、协作锁或进程名扫描不能提供强隔离；普通应用无法控制设备系统 cgroup。不得通过移除拒绝、重签未知运行树或关闭检查冒充完成。现有测试版克隆功能同样关闭。
- 本机测试临时目录 `E:\redmagic-1b-review-g3vo4j1d` 仍保留。回滚时逐项恢复本轮源码和文档并移除本轮新增文件，保留此前已有工作树改动；设备测试实例和副本需单独确认后处理。

### 任务 1B 需求变更：按 fingerprint 精确匹配（2026-11-04）

- 任务范围调整为简洁的 Guest 声明指纹准入，两条完整值任意匹配即通过：系统分区 `20260130.143527`、bootimage `20260130.145957`；完整值及属性名见 [TARGET_FIRMWARE.md](TARGET_FIRMWARE.md)。它们是同一个指定包的不同分区信息。比较区分大小写、忽略值首尾空白，不匹配或缺失则拒绝；不判断宿主属性。
- 导入在兼容处理前检查并保存实际匹配的值；启动及快捷方式复用同一判断，旧实例没有该字段时补读系统属性。没有独立备份恢复功能；未新增其它 ROM 导入入口。撤销旧方案的 SHA 准入、Keystore 基线、目录摘要及统一拒绝启动门槛，保留导入格式、路径、清理等基础保护。此规则只能识别声明。
- 保留前轮已允许的 APEX 和分区移动链接修正；此前未经任务要求的克隆总拒绝已解除并保留路径边界检查。除任务 1B 外其他阶段的目标与验收标准不变。
- 最终验证：JDK 21 运行 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 成功，131 项测试中 99 项通过、32 项因本机符号链接权限等条件跳过；指纹精确匹配、空值、单字符差异、首尾空白和旧实例回退的定向测试通过。标准 Debug APK 位于 `app/build/outputs/apk/standard/debug/app-standard-debug.apk`。
- 设备验证：在独立 `app.redmagicemu.clone` 测试包中导入指定 ZIP 成功，实际保存 `143527` 系统指纹，临时待处理数据清理；快捷启动通过准入并进入 Guest 启动流程，随后 `zygote exited (code 255)`，Guest 未启动成功。未对该兼容故障作因果判断或继续排查；设备测试包与最终标准 APK 的拒绝提示文字有差异，其准入逻辑相同。原应用未覆盖。
- 先前设备验证曾在取消旧基线后因发布步骤仍要求基线而失败；已移除该依赖并新增回归测试，本次设备导入已验证修复。设备测试实例及其固件副本保留。本机 `E:\redmagic-1b-review-g3vo4j1d` 临时副本仍保留。

### 0.1.0 任务 1C：单系统界面与阶段审查（2026-11-04，未完成）

- 阶段分支 `1c-single-system`，保留原有未提交的 1B 工作和日志删除。主页改为加载、未导入、导入中、准备完成及历史多实例冲突状态；移除库、克隆及通用引擎等界面入口，保留显示、输入、音量、停止和诊断相关功能。未提供备份恢复占位；历史多实例不自动删除。
- 底层发布锁内拒绝第二个已发布实例，删除实例克隆路径；新增测试先复现第二次发布可成功，再验证拒绝。导入仍依据 ZIP 结构、所提取条目的 CRC 和指定构建指纹。正式入口统一执行指纹检查。1B 的设备导入及启动失败证据见上一节，本轮未重新安装或运行设备。
- 同一 VM 进程内 Activity 重建不再自动重试失败；前台通知使用中性进程文案。进程被杀后上次失败未持久化；旧多实例冲突无界面处理路径。删除运行中实例的保护尚未核实。取消、空间不足、重复点击、进程强杀和恢复的完整设备流程待检查。
- **存在缺陷：** `GuestVm.boot()` 检查和状态设置非原子，重复启动可能竞争；准备期 `stop()` 可能先设置停止而启动线程随后继续。正常系统的 RUNNING 基于 Guest 启动完成属性；缺少通用启动超时。指定固件此前 `zygote` 退出、未进桌面；不能声明启动成功。备份恢复尚无经过统一校验的实现。
- JDK 21 执行 `:app:testStandardDebugUnitTest :app:assembleStandardDebug :app:assembleCloneDebug` 成功；137 项测试中 105 项通过、32 项跳过，0 失败。首次调用受本机失效的 Gradle Java 路径配置阻碍，使用命令行指定 JDK 后成功。`git diff --check` 通过；构建有既有 Kotlin 警告。未实机验证本轮改动。当前构建版本仍为 0.0.0.4，0.1.0 仅为阶段目标；未发布、推送或打标签。
- 回滚 1C 时仅逐项恢复本轮 UI、单实例发布与对应测试改动，保留 1B 及其他原有未提交内容；不要重置工作树或删除实例数据。

### 任务 1C：准备期启动与停止竞态（2026-11-04）

- 设备通过无线 ADB 连接；只读检查已安装的独立 `app.redmagicemu.clone`，版本 `0.0.0.4`，安装时间 23:16:46。其实例 `fnjnbt` 元数据声明 NX809J、API 36、arm64 和已接受的系统指纹。未重启 Guest、安装 APK 或修改设备数据。
- 该实例现有运行日志在 23:34:15 依次记录 `stopping system`、等待显示和 GL bridge 超时、随后 `surfaceflinger started`。这与源码中准备期停止后启动工作仍可继续的竞争一致；采样记录止于 surfaceflinger 启动。此次运行未进入桌面。
- 源码检查：`GuestVm.boot()` 的状态检查和 PREPARING 设置未原子化；`stop()` 可在准入完成前返回，而后续启动仍可能创建资源。修复需协调准备线程、资源创建和清理，并以受控并发测试及设备停止检查验证；单次取消检查不能覆盖后续资源创建竞态。本轮仅诊断，缺陷仍存在，1C 不通过。原始日志未保存或加入仓库。

### ARM64 启动引擎选择回归修复（2026-11-05）

- 原版 AE 日志记录 Native arm64 引擎与 Guest `unplug` 关机；此前 RedMagicEMU 独立测试实例则保存 KK 引擎，启动时跳过了原有 `effective()` 选择，设备日志显示旧引擎加载 `app_process64` 报架构不匹配，随后 zygote 退出 255。这与 1B 中删除启动前引擎选择的改动吻合；尚未做修复后的设备对照，不能断言 Guest 将启动成功或复现原版关机。
- 在 GuestVm 创建时统一调用原有 `effective()`，让 ARM64/API 36 使用 N64 引擎，同时不改保存的镜像元数据或指纹准入。新增 VM 与 runner 路由测试，旧代码先失败、修复后通过；不修改原生引擎或其它兼容策略。
- JDK 21 执行 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 成功；153 项中 121 项通过、32 项跳过、0 失败。未安装或实机运行该构建。标准 Debug APK：`app/build/outputs/apk/standard/debug/app-standard-debug.apk`。回滚仅撤销 GuestVm 构造时的 effective 选择及对应新增测试，不动其它未提交内容。

### 任务 1C 继续：并发、状态和导入收敛（2026-11-05）

- 保留 1B 的 ARM64 `effective()` 引擎选择及既有工作树内容。启动预约与停止协调；停止请求阻止新的受管进程创建、终止已登记命令、等待启动和受管任务，清理失败不允许重启。并发停止及停止请求抛异常均可重试；受管进程终止后等待实际退出。ADB 关闭连接并等待工作线程，避免同步流在停止返回后继续工作。未登记的后代进程无法据此证明全部退出。
- 用受控线程与本地 socket 测试复现并验证停止、ADB 同步流和受管进程边界；这些不是 Android 上真实 Guest 的完整集成验证。删除和重置实例暂不可用，底层也拒绝；历史多实例准入拒绝启动但没有安全整理界面，不自动删除数据。恢复模式入口移除，底层拒绝恢复启动。
- 导入取消和发布共用门禁：取消先到则不发布，发布先完成则结果为成功；完成后列表刷新期间保持加载状态。历史启动中断/失败记录随 VM 而非页面存活，停止失败显示错误并可重试。系统仅报告启动完成时提示桌面尚未确认；准备或启动超过五分钟显示未确认及等待、停止、重试，不把超时断言为故障。状态订阅使用原子快照，Guest 关机状态随 VM 保留以防页面重建自动重启；Activity 重建、真实 I/O 异常和这些交互尚未设备验收。
- JDK 21 执行 `:app:testStandardDebugUnitTest :app:assembleStandardDebug :app:assembleCloneDebug` 成功；171 项测试中 139 项通过、32 项跳过、0 失败；`git diff --check` 通过。构建包含弃用警告。
- 设备在线，原独立 clone 测试包不存在，标准包保有一个已有实例；尝试安装新 clone 返回 `INSTALL_FAILED_USER_RESTRICTED: Install canceled by user`，未安装或覆盖标准包。未再次尝试。1B 的引擎修复后指定固件启动、1C 停止/取消/空间不足/进程重建及桌面列为设备检查项；不能据构建或 Guest 属性宣称桌面启动成功。
- 1C 尚未达到设备验收或发布条件；备份恢复无经过统一校验的实现且不显示入口。16 KB 兼容和历史实例整理仍未解决。没有推送、合并、打标签或发布。回滚仅逐项撤销本轮相关源码、测试及文档改动，保留 1B 修复及先前未提交内容，不重置工作树或删除设备数据。

### 任务 1C 补充：root 安装独立测试包（2026-11-05）

- 本轮使用 Root 安装；通过设备 Root 安装 `app.redmagicemu.clone` 调试包成功，已安装 APK 的 SHA-256 与本地 clone 构建一致。临时传输的 APK 已删除；标准应用仍已安装，未覆盖或清除其数据。
- 本次只验证安装和 APK 摘要，未启动应用、导入固件或测试 Guest；1C 实机运行及目标固件仍列为设备检查项。此前普通安装被设备取消的记录保留。

### 任务 1C 补充：指定固件设备测试（2026-11-05，未通过）

- 设备小米互传目录中的 `RedMagic_NX809J_VM.zip` 通过文件选择器导入独立 `app.redmagicemu.clone` 测试包；设备文件 SHA-256 为 `fc66fe1de4759efb66b6c8070b7cd0ca9f83b91a6d93704a444ecb7ef70009cb`，此值仅作本次文件识别，当前准入仍是构建指纹而非该摘要。原包未修改。
- 空状态显示“未导入”和指定固件入口；实际导入完成，显示“准备完成”，生成实例 `xf2niq`，保存目标 `143527` 系统指纹。待处理目录仅余管理锁，磁盘可用空间从导入初约 120 GB、提取时约 75 GB 回到约 102 GB。导入成功卡片仍有通用引擎、Android 实验提示，属于界面遗留缺陷。
- 两次启动的日志均显示 Native arm64、kernel 36，确认 1B 的 ARM64 引擎选择修复在该设备路径生效；两次均在启动阶段出现 `guest requested power off (unplug)`，界面显示 `Guest powered off`，未进入桌面。第二次日志记录部分服务异常退出；未进行 HAL 修改。重试可触发第二次启动。
- 关机后只读进程快照未看到命令行含该实例路径或 `libaemu64_36` 的进程，应用主进程与 `:vm` 进程仍在；未测试准备期手动停止、取消、空间不足、Activity 重建或输入显示音量，仍列为设备检查项。应用测试包保留在关机画面，实例与设备原始包保留。
- 启动应用及 VM 进程时设备重复显示已知 16 KB 对齐警告；只点击“确定”，没有禁用提示或调整系统策略。通知权限确认后测试包获授权。标准应用未覆盖或清除数据；原始日志未保存到仓库。1C 设备验收未通过，启动关机原因待进一步诊断。

### 任务 1C：启动同步和导入状态验证（2026-11-05）

- 联合验证覆盖停止时的同步进程登记、模拟空间不足、Activity 状态恢复后的初始 Intent 去重，以及导入完成页的引擎提示。
- JDK 21 执行 `:app:testStandardDebugUnitTest :app:assembleStandardDebug :app:assembleCloneDebug` 成功；176 项测试中 144 项通过、32 项跳过、0 项失败。`git diff --check` 通过；空闲空间和 Intent 重投测试分别使用模拟空间不足及 Activity 保存状态。
- 第二次导入触发取消后，界面仍显示暂存或检查。无线 ADB 离线及重连失败使本次验证未能取得导入结果、待处理目录和原实例状态，取消行为待复测。
- 后续设备检查包括取消、准备期停止、空间不足、重复操作和进程重建，以及历史多实例安全处理；未排查 Guest 无法进入桌面的原因。本轮未安装 APK、清理设备数据或发布。

### 任务 1C 补充：暂存阶段取消设备检查（2026-11-05）

- 设备恢复无线 ADB 连接后只读检查：独立测试包中仍只有目标实例 `xf2niq`，其元数据为 NX809J、API 36、ARM64 与目标 `143527` 指纹；待处理目录仅有管理锁。上次第二次导入的结果不再可见。
- 使用已有文件选择授权，对同一目标 ZIP 发起一次受控导入；在界面显示 `Staging firmware` 时，核对取消按钮可点击区域并点击。界面随后显示“导入失败 / cancelled”和原实例“准备完成”；待处理目录仅剩管理锁，实例列表仍只有 `xf2niq`，设备约剩 100 GB。该次暂存期取消与清理已在设备观察；“导入失败”文案未区分用户取消，是界面待改进项。
- 此次未测试解包中取消、发布竞争、进程强杀、其他存储失败或准备期停止；不把单次暂存取消扩展为完整 1C 验收。未重启 Guest、安装 APK 或删除已有实例与固件。

### 任务 1C 补充：区分用户取消与导入失败（2026-11-05）

- 针对设备上主动取消显示“导入失败 / cancelled”的问题，导入链使用明确的取消异常，界面显示“已取消导入”；普通 I/O 错误、临时数据清理失败仍按失败显示，完成发布后的迟到取消不改写成功结果。原有固件准入与数据保留规则不变。
- 暂存和发布取消测试修改前分别断言失败；修改后相关定向测试通过。JDK 21 联合执行 `:app:testStandardDebugUnitTest :app:assembleStandardDebug :app:assembleCloneDebug` 成功；180 项中 148 项通过、32 项跳过、0 失败。`git diff --check` 通过。
- 本轮未安装新 APK 或在设备复测新文案；解包中取消、发布竞争、设备进程重建及其他 1C 项目仍列为设备检查项。回滚时仅撤销本次取消类型、状态、文案与相关测试，保留其他未提交修改和设备数据。

### 任务 1C 补充：取消文案实机复测（2026-11-05）

- 使用 Root 覆盖安装独立 clone 调试包，设备 APK SHA-256 与本地一致；实例 `xf2niq` 仍存在，未覆盖标准包。旧文件选择授权在安装后失效；未授予共享存储所有文件权限，使用指定 ZIP 的私有临时副本复测。副本属本次测试，结束后已删除，原固件保留。
- 暂存阶段点击取消后，设备显示“已取消导入”，原实例仍显示“准备完成”；待处理目录只余管理锁，实例列表仍只有 `xf2niq`，测试副本清理后空间约 100 GB。此结果只覆盖暂存期一次取消。
- 尝试准备期手动停止时，操作未能可靠核实命中“关闭系统”；一次停止日志后出现 PendingIntent 启动，亦可能误触 Reboot，另一次未找到关闭按钮，Guest 随后自行请求关机。现有证据不足以判定停止通过或代码异常；后续须核对点击元素及停止前后状态。最终快照中未见命令行含该实例或目标引擎的进程。
- 历史多实例仍拒绝启动且不自动删除；仅用短期文件锁无法排除崩溃后残留 Guest，安全整理需要生命周期及崩溃恢复协调，尚未实现。真实 ENOSPC、准备期停止、进程重建及历史实例整理仍列为设备检查项，1C 未判通过。未排查无法进入桌面的原因。

### 任务 1C 补充：准备期停止验收诊断（2026-11-05）

- 设备上标准应用的 `:vm` 未运行，独立 clone 实例 `xf2niq` 存在。尝试从 Guest 关机画面重新启动并通过菜单停止；兼容性提示及 Guest 在短时间内自行关机，未能在确认 PREPARING 状态后可靠点击“关闭系统”。没有把启动中或关机后的操作计为准备期停止通过。
- 只读复核：现有 `VmLifecycleTest.stopWaitsForBootAndRejectsLateResourceCreation` 覆盖协调器中的受控启动/停止，但未调用完整 `GuestVm.bootAsync()` 和设备路径；停止清理还可能有单独的 Guest sync 命令。完整设备验证需要可控地阻塞准入、确认 PREPARING、请求停止并释放阻塞，再检查停止结果及后续资源。设备进程扫描只记录快照内的进程。
- 本轮未改产品代码、未安装或删除数据，未排查 Guest 无法进入桌面的原因。准备期手动停止仍列为设备检查项，1C 未判通过。

### 任务 1C：准备期停止的验收范围（2026-11-05）

- 1C 已完成准备期停止协调器的本地测试。准备期手动停止的设备验证和原厂 ROM 桌面启动验证均延期。

### 任务 1C：空间不足测试范围（2026-11-05）

- 1C 覆盖空间预检和模拟空间不足。设备实际写入返回 `ENOSPC` 的场景待验证。

### 任务 1C 补充：就绪状态进程重建检查（2026-11-05）

- 在独立 clone 测试实例 Guest 已关机、未观察到目标引擎进程时，对测试包执行系统强制停止后从启动器重新打开；系统兼容性提示仅点“确定”。重开后界面显示“准备完成”和指定固件，`xf2niq` 仍存在，未观察到 clone `:vm` 进程，待处理目录只余管理锁。
- 这是就绪态强制停止后重新启动的设备观察；未声称原实例全部数据内容逐字节不变。未覆盖标准应用或删除数据。
- 历史多实例仍安全拒绝启动并保留数据；生命周期锁在进程崩溃时释放，但未登记 Guest 后代可能仍存活，安全整理需要额外的崩溃恢复协调。真实设备空间耗尽验收已移出本阶段，现有检查保留。

### 任务 1C：历史实例处理范围（2026-11-05）

- 1C 采用单实例导入与启动准入。历史多实例的整理、迁移和崩溃残留处理仍为待定项。

### 任务 1C 补充：启动入口连续点击检查（2026-11-05）

- 在标准应用 `:vm` 未运行、独立 clone 主页显示目标实例“准备完成”时，连续两次点击“启动”。系统 Activity/进程记录在观察窗口内显示一次 `VmActivity` 启动及一个 clone `:vm` 进程，实例列表仍只有 `xf2niq`；Guest 随后按已知情况请求关机，界面显示 `Guest powered off`。
- 第二次点击时界面可能已切换。既有 `VmLifecycleTest.duplicateBootAndBootDuringCleanupAreRejected` 覆盖底层协调器重复启动及清理期间启动。未安装、删除实例、修改固件原包或排查 Guest 关机原因。

### 任务 1C：重复启动测试范围（2026-11-05）

- 重复启动协调已通过单元测试。连续点击的设备观察已记录，完整端到端并发验证不列入 1C 验收。

### 任务 1C 补充：固件提取阶段取消设备检查（2026-11-05）

- 在独立 clone 测试包中，以指定 ZIP 的私有测试副本发起导入；确认界面进入 `Extracting firmware` 后点击实际可点击的“取消”。界面显示“已取消导入”，待处理目录仅余管理锁，原实例列表仍只有 `xf2niq`。
- 删除本次私有测试副本后，设备可用空间约 100 GB；原始固件及原实例保留。本次验证覆盖一次固件分区树提取阶段取消与临时数据清理；未启动 Guest、安装新包或修改产品源码。

### 任务 1C 补充：导入暂存中进程结束与恢复（2026-11-05）

- 在独立 clone 测试包通过私有副本导入指定 ZIP，确认界面显示 `Staging firmware` 且待处理租约目录存在后，仅向 clone 主进程发送 SIGKILL；以明确的启动器 MAIN Intent 重新打开。主进程由 PID 12854 变为 31949，未观察到 clone `:vm` 进程。
- 重开后待处理目录只剩管理锁，实例列表仍只有 `xf2niq`；该实例 `image.json` 的 SHA-256 在测试前后相同。界面显示“准备完成”，没有自动再次导入或启动，也没有提示上次导入中断。测试私有副本已删除，原固件及原实例保留，设备可用空间约 100 GB。
- 本次只覆盖暂存时人为结束主进程后的恢复；仅比较实例元数据摘要。导入中断没有提示这一用户体验需后续评估。未修改产品代码或排查 Guest 桌面问题。

### 任务 1C 补充：导入中断提示与阶段收口（2026-11-05）

- 恢复清理确认存在未发布的中断租约时，在清理前记录中断提示；提示跨进程保留至明确确认，使用代次避免确认旧提示时清除较新事件。已发布实例、活跃租约及正常关闭遗留不作为中断提示；恢复失败使用独立错误状态和重试。提示不声称临时数据必然已清理。
- 独立 clone 调试包经 root 覆盖安装，设备 APK 摘要与本地一致；在 `Checking firmware package` 阶段结束 clone 主进程并重新打开，界面显示“上次固件导入中断，请检查导入状态”，待处理目录只剩管理锁，`xf2niq` 元数据摘要与测试前相同。未确认提示时再次结束进程并重开，提示仍显示；确认后重开不再显示。原始固件、原实例保留，私有测试副本已删除；未启动 Guest。
- JDK 21 联合执行 `:app:testStandardDebugUnitTest :app:assembleStandardDebug :app:assembleCloneDebug` 成功：187 项中 155 项通过、32 项跳过、0 失败。`git diff --check` 通过。定向测试覆盖恢复、确认代次和错误状态；进程恰在正常关闭标记写入后结束的边界为避免误报取消/失败，不提示中断，但仍尝试安全清理，未做设备故障注入。
- 按最终确认范围，单系统界面、指定固件准入与导入、取消、非 Guest 的就绪与导入中断恢复已完成当前阶段检查；准备期停止和所有 Guest 运行问题留待后续版本，真实设备空间耗尽、重复点击和历史多实例整理不再作为 1C 阻碍。备份恢复尚未实现且无可用入口。没有创建 release、推送、合并或打标签。回滚本次提示功能时逐项恢复本次相关源码、字符串和测试，保留其余未提交工作及设备数据。

### 0.2.0 启动修复：正常启动误拉起 ztecharger（2026-11-05）

- 沿用 1C 最终确认范围。同一 NX809J clone 实例在修复前 12:21 使用 Native arm64 / kernel 36，启动 ztecharger 当秒收到 unplug 关机。旧 APK 的附加跟踪改变了时序，仍复现 unplug，未捕获 socket 写入进程。
- 固件 argv 为 /system/bin/ztecharger，无额外参数。class ztecharger 由 on charger 启动，sys.boot_from_charger_mode=1 停止该 class 并触发 late-init。反汇编确认该程序的 unplug 分支调用生成 sys.powerctl 请求的函数。模拟器此前把它作为正常开机服务启动。修复后读取 ro.bootmode=unknown，虚拟 AC 在线、USB 离线、电池存在且满电 100%。普通 vendor.health-default 启动，实际 Binder HealthInfo 尚未确认。相同 Health 二进制的 vendor.charger --charger（class charger）仍启动，本次单独保留以隔离变量。
- 正常计划排除 /system/bin/ztecharger，服务启动入口同时覆盖保存的旧计划、显式启动与重启路径。回滚仅撤销这两处判断及对应测试，无需恢复固件文件。关机请求、原因记录和宿主停止仍走原处理路径。定向测试修改前因多出 ztecharger 失败，修改后通过；JDK 21 标准单测及标准/clone 调试构建通过，188 项中 156 项通过、32 项跳过、0 失败。
- 经 root 覆盖安装独立 clone，设备与本地 APK 摘要一致。13:12 新启动越过原关机点，未启动 ztecharger，普通 Health 与 vendor.charger 仍启动。到 13:15 仍在启动供应商服务，未见自发 unplug；未见 zygote 启动，Guest sys.boot_completed 读取为空，未确认 system_server、SystemUI 或原厂桌面。通过 Guest 运行器发出的受控 shutdown,userrequested 在观察窗口内未返回，已结束诊断命令。宿主菜单停止未取得成功回执，两项均未通过实机验收。
- VINTF 与启动计划部分不一致。运行树保留 Health 片段；QTI display allocator/composer 与 thermal 片段已隔离，但对应 vendor rc 服务仍开机启动。按需 AIDL 扫描不直接读取 vendor rc，按名称的 ctl.start/ctl.restart 可从启动计划取服务；崩溃重启查保存的服务列表及部分重算计划，未按 VINTF 统一过滤。未观察到上述三个服务被按需或自动重启。本轮只修复 ztecharger 启动，原始日志和固件诊断副本未纳入 Git。

### 0.2.0 手机重连复查（2026-11-05）

- 无线 ADB 重连后复查同一 clone APK（SHA-256 与上次构建一致）及同一次启动。13:20:55 zygote 启动，13:21:55 退出 137。该次运行未见 ztecharger 或 unplug，Guest sys.boot_completed 仍为空，未取得 SystemUI 持续运行或原厂桌面证据。
- Guest DropBox 在 13:21:55.135 写入 system_server_crash。Nubia Cube DataCenter 初始化抛出 ExceptionInInitializerError，底层为 cn.nubia.server.cube.cpuperf.T.<clinit> 对 null 数组取长度。该记录确认 system_server 实际崩溃，不能单凭退出码认定宿主内存不足。尚未追踪数组来源。
- 停止日志在 13:24:07 和 13:24:14 出现，最终快照未见 clone VM 或匹配的 Guest 进程。此次复查未执行停止点击，不能凭日志确认操作来源。未重启、安装或修改启动代码，原始设备日志未加入仓库。

### 0.2.0 原厂显示设置设备复测（2026-11-05）

- 独立 clone 实例保存 1216×2688、520 DPI，另存内存 12288 MB、存储 131072 MB。本次未修改上述设置。13:32 的上一轮启动日志记录 framebuffer 1216×2688；13:42 新一轮启动使用该配置，运行中的 Guest 属性 ro.sf.lcd_density 与 qemu.sf.lcd_density 均为 520。实际桌面显示尚未验证。
- 新一轮 13:45:04 启动 zygote，13:45:17.921 的 Guest DropBox 记录 system_server 因 Nubia Cube cpuperf 初始化对 null 数组取长度而崩溃，13:45:18 zygote 退出 137。未观察到 sys.boot_completed=1、SystemUI 或原厂桌面，未发现 ztecharger 或 unplug 记录。具体数组来源尚未定位。
- 在启动失败界面选择关闭，应用报告 Guest process did not exit after termination，重试后同样失败。快照仍有 clone VM 进程及其 battery_record_daemon、trusteduilistener 子进程。没有强制结束这些进程或更改宿主策略，停止功能本次未通过。原始日志未加入仓库。

### 0.2.0 Cube SoC 属性与停止诊断（2026-11-05）

- 对指定固件 cubeserver.jar 字节码检查，cpuperf.T 静态初始化按 SOC_VENDOR 和 ro.soc.model 查找数组；Guest 的 ro.soc.model 为空，固件映射含 qcom/SM8850，system_ext 配置含 SM8850。临时只补 SM8850 后 system_server 越过先前 CPU 空数组崩溃，日志出现 SystemUI 和原厂启动器进程，尚未确认桌面可见及持续运行。
- 启动准备在已准入 NX809J/API 36 上为缺失或空的 ro.soc.model 补 SM8850，保留已有非空值。未修改原始固件，回滚只需移除 GuestVm 调用、TargetSocProperty.kt 和对应测试。真实属性区的缺失、空值及已有值测试通过；设备安装构建后读取 SM8850 成功，但尚未取得该构建到桌面的验证。
- 临时 CPU 属性实验之后，com.android.phone 因 telephony provider 未被包扫描加载而崩溃并触发 RescueParty。原厂 qspa_default.rc 在 init 设定 ro.boot.vendor.qspa.modem=enabled，而当前 Guest 未加载该导入。临时设置该属性的试验因启动缓慢、手机明显卡顿而中止，未到 zygote，不能断言该属性解决问题或造成卡顿；临时 props.extra 已删除。
- 手动关闭方面，本机构建将所有已启动的服务进程登记并在停止时强制终止，修复总等待限时耗尽后对已退出进程误报的问题；定向测试通过。该构建设备尝试关闭仍报告 Guest process did not exit after termination，两个 Guest 服务残留。使用 clone UID 的 SIGKILL 可清理这两个明确进程，尚未确认自动终止失败根因；之后添加失败时存活进程名和 PID 诊断，尚未设备验证。应用已强制停止后，观察不到 clone VM 和匹配 Guest 引擎进程。
- 停止前宿主 uptime 报告负载均值异常高，可用内存约 7.8 GB、`/data` 约剩 88 GB，无法仅凭这些指标确定卡顿原因。鉴于设备稳定性风险，未继续启动 Guest。JDK 21 标准单测与标准/clone 构建通过，193 项中 161 项通过、32 项跳过、0 失败；最终诊断构建未安装。未验证启动完成、桌面、强制停止完整路径或原厂 modem 属性效果。

### 0.2.0 低内存模式运行检查（2026-11-05）

- 低内存模式设置项已开启，独立 clone 设置保存 `lowRam=true`，原有 12288 MB 内存、131072 MB 存储及 1216×2688/520 DPI 保留。14:15 启动后 Guest 读取到 `ro.config.low_ram=true`、`ro.soc.model=SM8850`。
- 启动约一分钟时手机 1 分钟负载均值由约 26 升至 1229，可用内存约 6.7 GB。界面未能可靠打开关闭菜单，为保护设备执行系统强制停止独立 clone，随后未见该 VM 及其已观察到的直属 Guest 进程。不能据单次采样判断负载根因或认定 ROM 容量导致卡顿。
- 本轮原厂 vendor.bluetooth-aidl-qti 启动后日志连续出现 shadow stack 分配失败、内存映射失败及 fatal signal 6，其 VINTF bluetooth_hci 片段此前已隔离。该现象与负载上升的因果关系尚未通过隔离试验确认。zygote 尚未启动，低内存模式未使本次 Guest 进入桌面。手动停止的失败诊断因菜单未能打开而未获得结果。

### 0.2.0 隔离 QTI 蓝牙 HAL 与低内存设备复测（2026-11-05）

- 上一轮低内存启动中，Guest 蓝牙服务 PID 8110 在宿主日志里由数百线程增至最多 11188，服务日志连续出现 shadow stack 分配失败。该固件的 bluetooth_hci VINTF 片段已隔离，但 vendor.bluetooth-aidl-qti 仍在启动计划中。线程暴增与此前手机负载异常同时出现，不能从单次对照证明全部负载来源。
- 对 API 36 的 /vendor/bin/hw/android.hardware.bluetooth@aidl-service-qti 同时过滤常规计划和统一启动入口，覆盖旧计划、按需和重启调用。其他服务继续按原计划处理。回滚仅撤销两处判断、InitPlan 对应谓词与新增测试。定向测试修复前因蓝牙服务仍被加入计划而失败，修复后通过；JDK 21 标准单测与标准/clone 调试构建成功，194 项中 162 项通过、32 项跳过。
- 独立 clone APK 摘要与构建一致。手机设置保存 lowRam=true，启动中 Guest 属性 ro.config.low_ram=true；14:20:21 启动、14:20:58 报告启动完成，实际读取 sys.boot_completed=1。新运行未见 QTI 蓝牙 HAL 启动日志，普通 Health 和 aemu 蓝牙替身启动。启动前 1 分钟负载均值约 45.88，14:23:24 为 7.93；这只是单次观察。
- 14:23:24 的宿主进程快照，clone Guest zygote、system_server、SystemUI 和原厂 launcher 均存活，与 14:21 附近日志中 SystemUI/launcher 进程一致；该窗口未见 zygote 退出或 Guest 关机重启。截图曾显示原厂首次开机欢迎页，点击开始后后续画面仅显示局部内容，不能确认桌面可见或触摸引导成功。vendor audio、sensor HAL 仍有崩溃/重启日志。系统此时仍在运行；未进行停止操作或验证强制退出。测试屏幕临时文件已删除。

### 0.2.0 关闭低内存后的启动对照及验收核对（2026-11-05）

- 独立 clone 设置 lowRam=false，保留 12288 MB 内存、131072 MB 存储及 1216×2688/520 DPI。14:26:39 启动，14:27:14 报告启动完成；Guest 读取 `ro.config.low_ram` 为空、`sys.boot_completed=1`。首次引导完成协议与隐私选项，测试时跳过账号、设备安全配置和数据迁移，并关闭引导页中自动下载和夜间安装更新的选项。
- 实际画面显示原厂桌面，Guest activity 查询的前台为 com.zte.mifavor.launcher 的 QuickstepLauncher。14:39:45 同一 Guest 的 zygote、system_server、SystemUI、launcher 仍存活，Guest 再读 sys.boot_completed=1，启动日志未见 zygote 退出或 Guest 关机重启。手机 1 分钟负载均值约 3.61；这是一台设备、一次启动的观察。
- 14:25 应用内停止上一轮 Guest 仍失败，诊断列出 brd_normal 和 vendor.trusteduilistener 存活；以 clone UID 向确认属于该 Guest 的两个 PID 发 SIGKILL 后重试停止才成功。源码审查发现 Android 16 UNIXProcess 的 destroyForcibly 走 SIGTERM，现有清理不足以保证结束忽略该信号的服务；可靠强制停止尚未修复和设备验收，合法 Guest 关机与退出原因仍缺修改后设备验证。
- VINTF 被隔离的 QTI display allocator/composer、thermal 服务此前仍在启动，QTI 蓝牙 HAL 已针对性阻止，尚未完成统一启动/按需/重启路径的一致性修复。关机时 unplug 请求的实际写入进程未被跟踪捕获，`ztecharger` 反汇编有对应请求路径，修改后这一观察窗口未再发生 unplug。壁纸与主题问题不纳入 0.2.0；原始日志和截图未纳入 Git。Guest 在核对结束时继续运行。

### 0.2.0 宿主 VM 进程异常记录（2026-11-05）

- 14:43:54 宿主 clone `:vm` 进程因 Compose `ScrollableTabRow IndexOutOfBoundsException` 退出，结束了 14:26 的 Guest 运行。现场记录显示为主动调用 Camera 导致，该问题留待后续处理；该次运行在此之前观察到桌面和核心进程持续运行约 16 分钟，不能称为无限期稳定。Camera 问题不纳入本轮修复。

### 0.2.0 强制停止与重启设备复测（2026-11-05）

- 为受管直属 Guest 进程增加 pidfd 强制终止和独立能力探测；不支持时退回原有终止方式。隔离探针、握手及信号的本地测试通过，JDK 21 标准单测与标准/clone 调试构建成功：209 项中 176 项通过、33 项跳过。pidfd 只覆盖受管直属进程；旧的残留及 zygote 后代清理仍有宽泛扫描、裸 PID 和遗漏后代的限制。握手身份校验存在极窄的 PID 复用及同 UID 冒充窗口，私有 PID 字段在其他宿主的可用性也未验证。回滚时只撤销本次 pidfd 启动器、进程封装、JNI 和相关测试改动，保留其他未提交工作。
- 已安装独立 clone 调试包并核对 APK SHA-256；15:17 启动过程观察到受管 pidfd，15:19 报告启动完成。但 Guest 随后请求 `reboot,RescueParty`，重启后可从持久记录读取该原因，进一步出现重复崩溃和重启。为终止循环，15:20 系统强制停止独立测试包；当时未见 clone VM 与匹配的引擎进程，不能由此证明所有 Guest 后代均已清理。未完成应用内停止或受控 Guest 关机设备回归。
- 15:18、15:19、15:20 的 Guest DropBox 均记录 system_server 处理 BOOT_COMPLETED 时在 SingleHandAdapter 调用 SurfaceControl.setZswSingleDimenInfo，因 displayToken 为空而崩溃；同一时段电话进程也反复崩溃，缺少 telephony provider。Guest 持久设置显示单手模式未开启。尚未确定显示 token 为空的来源，也不能仅凭共时崩溃确定 RescueParty 的具体触发者；阶段验收未通过。原始日志、ROM 和个人数据未加入 Git。
- 15:25 的单次诊断启动中，SurfaceFlinger dump 显示内部物理 Display 0（HWC display 1），没有显示识别数据；模拟器 composer@2.1 默认服务已注册。采集时 display 服务尚未就绪，dump 记录的是采集时点的状态。15:26:09 的 sys.powerctl socket 同行凭据记录写入方为 system_server，内容为 `reboot,RescueParty`；这证明本次请求来源，不证明究竟哪个崩溃触发 RescueParty。收到重启后再次强制停止测试包，避免循环。物理显示 ID 为 0 是否与空 token 相关尚未确认。
- 为缩小显示查询范围，新增仅 API 36 主 binder 进程可用、默认关闭的诊断，检查指定进程的显示 ID/token 请求及回复，不记录原始 Parcel；删除运行目录 `binder.display.trace` 即关闭。解析及真实事务配对的本机测试通过，ARM64 构建与标准单测、标准/clone 调试包构建通过。大块共享内存请求不读取、不视为命中，回复保守记 unknown；身份匹配和记录可能改变时序。回滚仅移除诊断开关、binderd 诊断与测试并重建该二进制。
- 15:43 单次设备采集确认诊断开关生效，15:43:56 system_server 发出 `reboot,RescueParty` 后自动停止测试包；诊断未命中目标请求。此前一轮保护计时器过早到期，启动未完成便停止。诊断标记与临时保护脚本已删除，未完成最终启动和停止验收。
- 15:49 观察到测试实例再次启动，在其运行时只读检查 `/proc`：system_server 和 surfaceflinger 的 comm 都是 `libaemu64_36.so`，对应 cmdline 第二项分别为 `system_server` 和 `/system/bin/surfaceflinger`。此前只按 comm 精确匹配的诊断无法命中这些进程。该轮发生重启后强制停止测试包，以避免继续循环；此观察没有确定显示 token 的根因。
- 修正诊断身份匹配并增加固定上限的阶段标记后，本机解析与转发对照测试及两种调试包构建通过。15:54 设备新运行中仅观察到其他候选事务、UID 分类、inline 长度未确认和接口不匹配，未命中目标接口；15:54:57 Guest 再次请求 `reboot,RescueParty` 后保护程序自动停止测试包。仍不能确定显示查询的实际返回值；测试标记与临时保护脚本已删除。
- 对照 bclient 的发送格式发现，无 offsets 的 inline 数据没有尾部对齐填充，诊断误要求接收填充字节，导致合法请求被判不可读；该边界先复现失败再修正，本机测试、ARM64 重建和两种调试包构建通过，实际 Binder 转发未改。15:59 设备诊断已识别 system_server 到 SurfaceFlinger 的请求：多次 ID 查询返回一个 ID 0，两次服务端 token 回复含非空 Binder 对象。详细记录达到八笔上限后仍发生 `reboot,RescueParty`，不能推断崩溃时的查询结果或客户端收到的 token。保护程序停止测试包，诊断标记和临时脚本已清理。
- 时序复核：15:59 轮的 `com.android.phone` 连续崩溃发生在 15:59:40–44，`reboot,RescueParty` 请求在 15:59:46，而 SingleHandAdapter 的 system_server 空 token 崩溃记录为 15:59:47，晚于重启请求。该轮不能把空 token 崩溃当成重启请求的已证实触发原因；电话进程或其他包是否触发仍需 PackageWatchdog 证据。
- 实例设置隐藏单点触控开关及搜索至轨迹球的导航选择，保留返回、主页、最近任务、菜单，以及内存、存储、低内存、自定义分辨率。旧的导航及触控配置值未自动改写。当前设备实例保存 GPU、JIT、网络代理、HWUI 开启，完整 dex 优化关闭，额外 QEMU 参数为空；这些配置值不单独证明具体运行路径。
- 覆盖安装后在设备设置页实查，返回、主页、最近和菜单四个导航选择可见，单点触控和搜索至轨迹球选项未显示；原厂与自定义分辨率输入、内存和存储滑块、低内存开关仍可见。未更改或保存实例设置。
- 首次引导重置流程进入验证。现有 `resetData` 因未完成 VM 跨进程协调而安全拒绝，尚不能将删除数据的入口标为可用。设备确认 `app.redmagicemu.v0003` 没有已安装包或保留私有数据后，构建并安装该独立测试包、核对 APK 摘要，从 MiShare 中原始指定 ZIP 发起导入；不移动或清空现有 clone 的 `xf2niq`。切入后台后，独立测试包主进程仍存在，但 `cgroup.events` 显示 `frozen=1`，暂存 12 GB、提取约 664 MB，提取体积未继续增长。该导入目前在 Activity 的 ViewModel 协程内，没有导入专用前台服务；应用回到前台后能否继续、后台持续运行均未验证。未修改宿主冻结、温控或安全策略。
- 随后观测同一独立导入进程 `frozen=0`，提取与实例目录继续增长；未切换手机界面。重置按钮及其专门进程清理工作延后，继续优先启动修复；本轮为重置准备的未完成新文件已移除。

### 0.2.0 电话数据库 provider 启动条件（2026-11-05）

- 15:59 Guest 日志记录 `com.android.phone` 数秒内反复崩溃，RescueParty 对其执行 WARM_REBOOT。包扫描日志显示原厂 `TelephonyProvider_MFV.apk` 因缺少 `ro.boot.vendor.qspa.modem=enabled` 被跳过；APK 清单同时声明该条件及 `telephony` provider。固件 `qspa_system.rc` 通过 `ro.boot.vendor.qspa` 选择配置，默认配置的 `on init` 写入该属性；当前 init 解析器不跟随该导入，实例 `props.base` 也不含该键。
- 在已准入 NX809J/API 36 的启动属性准备中，仅当 QSPA 选择器缺失、为空或为 `default` 且 modem 属性缺失时补 `enabled`；保留已有值（包括空值）及其他变体。未改原厂 ROM；回滚只需移除该属性补值和对应测试，保留已有 SoC 补值。真实属性区缺失条件的测试先失败后通过，非默认变体测试先失败后通过；JDK 21 全量单测及标准/clone/v0003 调试构建通过，214 项中 181 项通过、33 项跳过。
- 设备可复测后，独立 v0003 的首次导入实例覆盖安装修复包，设备 APK 与构建摘要一致，实例 metadata 摘要不变。启动后 Guest 读取 modem 属性为 `enabled`，`pm path` 识别原厂包，`dumpsys package` 列出 TelephonyProvider；对 `content://telephony/carriers` 以空结果条件查询得到 `No result found`，说明该 authority 可访问。此前 `dumpsys activity provider telephony` 未匹配到运行中的 provider，不单独代表包未注册。
- 本次尚未到 `sys.boot_completed=1`，16:31:56–59 另有高通电话组件共用的 `com.qti.phone` 进程反复崩溃，栈为 `QtiRadioFactory.initQtiRadioAidl` 对空数组取长度；16:32:03 再次发出 `reboot,RescueParty`，保护程序停止独立测试包以防循环。Guest system 日志记录多个同进程 QTI 包的 RescueParty；不能认为本修复已解决整个启动循环。未验证桌面、持久运行、应用内停止或原有 clone 实例在该构建下的重启。

### 0.2.0 QTI 组件隔离诊断与宿主停止（2026-11-05）

- 在独立 v0003 实例下一次启动时，临时对用户 0 禁用 `com.qti.phone/.ExtTelephonyService`，未改产品启动代码或原始固件。16:46:15 实际读取 `sys.boot_completed=1`；16:46 至 17:02 的观察中 zygote、system_server、SystemUI、电话进程和原厂启动器存活，未见该轮 RescueParty 重启。完成首次引导后，原厂魔姬引导画面为黑色，手动切入原厂桌面；魔姬显示问题延后处理。该结果依赖临时禁用组件，不能当作正常配置下的启动验收。
- 测试后以 `pm default-state --user 0` 恢复该服务默认状态，并通过包限制文件确认不再含该组件的禁用记录；未覆盖引导期间产生的数据。恢复后短暂观察桌面和核心进程仍存活，但未重新启动验证默认配置。服务隔离不是已交付的正式修复；不能仅恢复 QTI Radio VINTF 声明或用对未实现事务返回成功的替身冒充正常服务。
- 17:05 从 App 菜单执行“关闭系统”，日志记录 `stopping system` 和清理 147 个进程；随后设备进程快照中该测试包 UID 只剩 App 主进程。该观察验证一次正常运行时的宿主菜单停止，不证明死机场景或全部后代清理，也未验证合法 Guest 主动关机及退出原因。日志仍出现音频及传感器 HAL 退出、重启；VINTF 和各启动路径一致性尚未完成。原始设备日志和截图未加入 Git。

### 0.2.0 QTI 组件默认状态对照（2026-11-05）

- 完成引导并恢复服务默认状态后，17:08 新启动于 `sys.boot_completed=1` 后不久收到 `reboot,RescueParty`；保护程序停止独立 v0003 测试包，确认在现有实例仍可复现，不是仅首次引导时的问题。
- 在该实例的运行树临时添加 Android `component-override` 配置，只改变 `com.qti.phone/.ExtTelephonyService` 的解析默认状态，不修改 APK、原始固件或用户组件状态。17:13 再次启动，实际读取 `sys.boot_completed=1`、前台为原厂 QuickstepLauncher，zygote、system_server、SystemUI 和 launcher 持续至 17:16；显式启动目标服务返回 `Not found; no service started`，`telephony` provider 仍可查询。本次仅覆盖已设置完的实例和约三分钟观察。临时配置已在 Guest 停止后删除。
- 17:16 从 Guest 属性服务发送受控 `shutdown,rme-test`，日志记录 `guest requested power off (rme-test)`，原因文件保留完整请求，随后记录停止及清理进程。这验证一次 Guest 属性请求路径。产品修复、构建及安装后设备复测仍待完成。
- 同一实例随后以符号链接形式临时提供相同配置，Guest 实际排除目标服务并达到 `sys.boot_completed=1`。17:26 通过原厂 Guest 电源菜单两次确认关机，日志记录 `guest requested power off (userrequested)`，持久原因文件为 `shutdown,userrequested`，并记录进程清理；下次启动日志读回该原因。临时实验链接与数据文件均已删除。该关机发生在临时配置实验中，正式构建还需复测。

### 0.2.0 QTI 单组件启动修复（2026-11-05）

- 在 `TreeFixer.fixup` 为已准入 NX809J/API 36/Native arm64 的运行副本生成 Android `component-override`，仅默认关闭依赖已隔离 QTI Radio HAL 的 `com.qti.phone/.ExtTelephonyService`，不改原 APK、原厂固件包、用户组件状态或关机处理。显式用户启用状态可能覆盖此默认值；QTI 扩展电话功能未实现。
- 配置先在同目录独占写入、同步并校验完整内容，再用相对符号链接以不覆盖方式发布。既有不同内容或不安全路径拒绝覆盖；进程在发布前中断可能留下未引用的 `.tmp` 文件，突然断电后的目录持久性未验证。Windows 的符号链接权限限制使对应单元测试跳过；手机 `run-as` 可建立同目录符号链接，原厂系统也已在临时设备对照中读取该形式。手机 `run-as` 硬链接被拒绝，产品未依赖硬链接。
- 回滚需移除本次 `TreeFixer.fixup` 调用及 `TargetQtiComponent` 代码和测试，并在 Guest 停止后检查运行树 `system/etc/sysconfig/aemu-nx809j-qti.xml`；仅当它是指向本机制单级 `.aemu-nx809j-qti-*.tmp` 且内容完全匹配的链接时，移除该链接和对应数据文件。无法确认来源的同名普通文件或其他数据应保留。原始刷机包无需恢复。
- JDK 21 全量单测与标准、clone、v0003 调试包构建成功，222 项中 186 项通过、36 项跳过、0 失败。独立 v0003 设备覆盖安装包的摘要与构建一致，安装前后实例 metadata 摘要一致；17:28 新启动由程序生成配置，17:29 Guest 实际读取 `sys.boot_completed=1`，显式启动该服务得到 `Not found`，原厂桌面可见。至 17:34 观察中 zygote、system_server、SystemUI 和 launcher 保持相同 PID，未见自发关机或重启；电话 provider 仍可查询。该次覆盖已完成首次引导的独立实例。
- 17:34 用原厂 Guest 电源菜单主动重启，日志记录 `reboot,userrequested`，下一轮启动读回该原因，复用同一配置并再次达到 `sys.boot_completed=1` 和原厂桌面；至 17:38 未见自发关机或重启。随后对同属测试包 UID、直属 Guest 的 `trusteduilistener` 人为发送 SIGSTOP，并确认其处于停止状态；点击 App 菜单“关闭系统”后日志记录停止及清理，测试包 UID 的进程快照仅剩 App 主进程。此测试覆盖一个暂停的受管服务，不证明整个 VM 死机或全部后代均可可靠清理。
- 17:39 再次启动正式构建的同一实例并读取 `sys.boot_completed=1`，17:40 通过 Guest 原厂电源菜单关机，日志记录 `guest requested power off (userrequested)`，持久原因文件为 `shutdown,userrequested`，随后记录停止和清理。诊断保护脚本、临时截图和命令输出已删除，正式生成的配置及用户数据保留。该设备、该实例在上述观察窗口达到了本阶段列出的桌面、启动完成、核心进程持续与无自发重启/关机条件；全新导入实例使用最终构建的首次开机、其他宿主、长时间运行和完整硬件 HAL 未验证。
- 本次 Guest `dumpsys battery` 显示 AC 在线、USB 离线、电池存在、满电 100%，`ro.bootmode=unknown`；它证明系统拿到这些电池状态，不单独证明 Health Binder 回复的具体来源。`dumpsys thermalservice` 显示 `HAL Ready: false`。已隔离 VINTF 的 QTI display composer 等仍存在开机启动记录，音频与传感器供应商 HAL 有退出重启记录；这些服务与 HAL 的按需及重启路径只按代码条件核对，不能一概认定已被拉起。未写宿主温控或真实硬件节点。

### 0.2.0 全新导入准备（2026-11-05）

- 完成全新导入验证后将版本名改为 `0.2.0-alpha`，并保持 clone 包名不变、执行卸载重装。已确认 clone 和其 Guest 进程未运行；卸载 `app.redmagicemu.clone` 成功，其原私有实例 `xf2niq` 随卸载清除，随后重装当前 clone 调试包，设备 APK 摘要与构建一致。原始 ZIP、标准应用及 `v0003` 独立实例保留；未安装新包名。
- 手机正在使用其他应用，已停止屏幕操作；全新导入尚未开始，版本名尚未更改，尚未提交或推送。GitHub 的 `fork/main` 已更新至 0.1.0 提交，整理 0.2.0 时须以该提交为基准并保留本地未提交改动。

### 0.2.0 全新导入及 alpha 构建复测（2026-11-05）

- 设备可用后，通过系统文件选择器重新导入原始 NX809J 固件 ZIP，完成准备；导入元数据为 Android 16/API 36、ARM64、1216×2688、520 DPI，低内存模式关闭。原始 ZIP 未修改。首次启动自动生成 QTI 单组件配置，实际读取 `sys.boot_completed=1`。完成原厂引导后魔姬页面黑屏，按 Guest 桌面键可进入原厂桌面；魔姬显示留待后续处理。首次启动中 zygote、system_server、SystemUI 和原厂 launcher 从 18:15 持续至 18:21，日志未见 Guest 主动关机或重启请求。
- 导入验证后将版本名改为 `0.2.0-alpha`、versionCode 改为 6，保留标准、clone 和 v0003 的既有包名。JDK 21 单测及三个调试构建通过：222 项中 186 项通过、36 项跳过、0 失败。退出首次启动实例后覆盖安装 clone，安装前后实例 metadata 的 SHA-256 一致；设备包管理器报告 versionCode 6 和版本名 `0.2.0-alpha`。
- 覆盖安装后的同一实例重新启动，18:26 读取 `sys.boot_completed=1`，18:27 解锁后实际显示原厂桌面；至 18:30，zygote、system_server、SystemUI 和 launcher 保持相同 PID，日志未见 Guest 关机或重启请求。18:30 从 App 菜单关闭，日志记录停止及清理，进程快照未见上述核心进程或 clone VM。宿主显示调试应用的 16 KB ELF 对齐兼容性提示，部分 vendor 音频与传感器 HAL 仍崩溃或重启；未解决旧 unplug 请求的实际写入者，未改变原厂关机请求处理，原始日志与截图未提交。

### 0.3.0 显示、触摸与文字输入初查（2026-11-05，未完成）

- 依据上节全新导入和 alpha 覆盖安装记录，指定固件在该设备的有限观察窗口内已实测启动至桌面；不扩大为长期或其他宿主稳定性。按优先级选定显示、触摸与文字输入，未选择其他模块。
- 在线设备运行独立 clone 的 0.2.0-alpha 既有实例：上滑解锁、点击设置和进入搜索页均有响应，画面可见；搜索框再次点击出现光标，但软键盘未显示。预期是点击文本框后显示可用键盘并能输入文字；该项未通过，尚未验证多点触控、旋转和实际文字输入。原厂魔姬首次引导黑屏继续延后处理。
- 源码路径：宿主 GuestScreen 将触摸坐标交给 InputService，经 input.sock 供 Guest 引擎模拟输入设备；Guest framebuffer 由宿主读取显示。这是模拟和宿主桥接，不证明原厂显示或触摸硬件 HAL 完整兼容。
- Guest 输入法服务 dump 显示已选中 com.baidu.input_oem/.ImeOemService，主连接存在但 mCurMethod=null、mBoundToMethod=false；服务连接记录为 DEAD，进程退出记录原因 UNKNOWN。使用已有“显示/隐藏键盘”菜单后重新点击文本框仍未显示键盘，之后读取 Guest secure show_ime_with_hard_keyboard=1。菜单会持久写入该设置；操作前未读取原值，不能可靠恢复或推断其原值。引擎报告完整键盘能力可能影响显示策略，但无法据此确定输入法进程退出原因。
- 本轮未找到根因，未修改产品源码、安装或重新构建，也未完成修复或回归；不能把文字输入判为通过。保留设备实例及固件，继续调查输入法退出原因后再进行单一变量修复与设备复测。

### 0.3.0 输入法退出原因继续诊断（2026-11-05，未完成）

- 继续检查上轮同一 clone 实例和既有运行日志。原厂百度输入法的初始化调用 WebView.setDataDirectorySuffix 时，后缀包含宿主 libaemu64_36.so 的绝对路径，因含路径分隔符抛 IllegalArgumentException；随后创建 ImeOemService 时因 BDCommon 未初始化而抛 IllegalStateException。该记录解释服务为何未建立，但尚未证明后缀取值的具体调用路径。
- 设备只读对照：Guest 执行 cat /proc/self/cmdline 得到的首项是宿主引擎路径，后续含 cat 和宿主运行树路径；宿主读取同一 Guest 的 system_server 命令行也以引擎路径为首项、system_server 为第二项。API 36 引擎对 /proc/self/exe 有特殊处理，未发现 cmdline 对应处理。上述现象与输入法异常的路径前缀一致，但尚未捕获该短命输入法进程自己的命令行或确认输入法如何取得后缀。
- 日志另有 /dev/null/Android/data 下目录创建失败，目前没有证据证明其导致上述初始化异常。本轮未修改产品源码、安装或构建，未宣称修复或验收通过；只生成并移除了本次两个临时诊断输出，未保存原始日志。下一步需确认输入法进程取名来源，并以定向失败测试及设备对照验证修复；保留上轮 show_ime_with_hard_keyboard 设置原值未知的记录。

### 0.3.0 输入法进程名修正及设备复测（2026-11-05，未完成）

- 指定固件输入法字节码表明：WebView 后缀来自进程名；当通过 ActivityManager 取名不可用时，回退到读取 /proc/<自身数字 PID>/cmdline。此前设备异常包含引擎路径，与该回退读取和内核记录的 argv 格式一致；未对此前那次运行的分支插桩。
- API 36 ARM64 引擎对当前进程数字 PID 的只读 cmdline 访问先按原路径和标志打开；仅在读到完整内容且第一项精确等于自身引擎路径时，返回移除该项的只读快照。其他 PID、其他标志、不匹配、不完整及资源失败保留原路径或原文件描述符；不修改宿主 proc 和原始固件。该快照与真实 proc 文件在元数据和更新时机上不同，其他命令行形态及调用方尚未完整验证。
- 定向测试先失败后通过，覆盖自身数字 PID 匹配、其他 PID、内容截取与异常输入；新一轮在手机 ARM64 重新执行通过。NDK 重建 API 36 引擎，JDK 21 标准单测任务及标准/clone Debug 构建成功（单测任务为 Gradle UP-TO-DATE，非本次重新执行）；旧版引擎以相同工具链独立重建的摘要与原仓库产物一致。
- 应用内关闭旧 Guest 后，root 覆盖安装独立 clone 测试包，安装 APK SHA-256 与构建一致；重新启动、解锁并打开设置搜索框，软键盘仍未出现。该轮日志采样中未见旧 WebView 后缀及 BDCommon 异常，但输入法两次因 TalosSPWrapper 使用空 Application 在网络工作线程崩溃，输入法服务仍未绑定。未对旧异常缺席作完整时间窗口证明，不能判定文字输入修复或模块验收通过；/dev/null 外部目录错误与新异常的因果未确定。
- 回滚本轮时仅撤销 run64.c 对 self_cmdline 的包含和处理，移除 self_cmdline.h 及其定向测试，恢复本轮前的 libaemu64_36.so 并重建 APK；不要重置其他既有未提交内容或删除设备数据。原始日志和固件未加入 Git。下一步查明新初始化异常及服务不绑定原因，实测键盘显示和文字输入；当前设备 Guest 保持运行。

### 0.3.0 输入法 self 进程名修正与设备复测（2026-11-05，未完成）

- 原厂输入法字节码显示 Talos 的 Application 仅在主进程初始化；其主进程判定优先读取 /proc/self/cmdline 并与包名比较。上轮仅处理数字 PID，设备 Guest 命令读取 self 仍以宿主引擎路径开头。输入法早前进程的内核命令行显示引擎路径后为包名及零填充；该证据与初始化分支相符，修复前未直接捕获该进程经 Guest 读取后的字符串。
- API 36 ARM64 引擎的现有只读兼容处理增加对精确 /proc/self/cmdline 的匹配，保留上轮数字 PID、原始打开权限和失败回退；不触及其他进程、宿主 proc 或原始固件。定向测试增加 self 及相近路径边界：手机 ARM64 旧逻辑先断言失败（退出 134），修正后通过；本轮另重新编译并执行通过。NDK 重建引擎，标准和 clone Debug 构建成功；此次未运行完整单元测试。
- 应用内关闭 Guest 后覆盖安装 clone，设备 APK 摘要与构建一致。新启动后 Guest 执行 `cat /proc/self/cmdline` 的首项为 `cat`，不再是宿主引擎路径，但其他参数仍保留。打开原厂设置搜索框后百度键盘和隐私协议实际显示；完成引导协议后取消可选登录。`input_method` 转储显示服务已绑定并报告键盘显示。
- 测试期间输入法至少一次发生触摸事件派发超过 8 秒的 ANR，系统结束该进程后重启；ANR 记录仅有主线程 futex 等待，没有可用 Java 栈，原因尚未确定。键盘重新出现，但本轮点击数字切换后又未能完成字符输入；因此文字输入和显示/触摸模块仍未通过验收。Guest 主 emulated 存储卷仍报告 UNMOUNTED，是否与输入法问题相关未证实，未改变存储或宿主安全策略。
- 回滚仅从 self_cmdline.h 移除本轮 self 路径匹配并恢复对应测试断言，再重建 API 36 引擎和 APK，保留此前数字 PID 修正及其他未提交内容。原始日志、截图与固件未加入 Git；Guest 在搜索页保持运行。下一步收集可用的输入法 ANR 线程栈并复现实际字符输入，再决定是否需要进一步修复。

### 0.3.0 输入法触摸超时诊断（2026-11-05，未完成）

- 设备测试恢复后，返回独立 clone 既有 Guest 的设置搜索框。原厂键盘显示后点击 ABC 键，字符未进入搜索框；输入法 PID 22893 发生 MotionEvent 派发等待 8009 ms 的 ANR，系统结束该进程。此前 PID 13725 和 21000 的两次 ANR 同样为约 8 秒触摸派发超时。
- 现有 ANR 文件报告无法连接 tombstoned，只有 futex 等内核等待。对确认属于测试包 UID 的输入法 PID 21776 受控发送一次 SIGQUIT，后续发现 ART 转储实际写入 Guest main 日志；此前未找到转储的判断有误。该转储显示输入法主线程在处理触摸后的按键界面更新中调用 SoundPool.release，等待 SoundManager 析构及 ThreadPool.quit；两个 SoundDecoder 线程等待 MediaCodecList::InstanceCache::getRemoteInstance 的互斥锁。另一应用线程在 MediaCodecList.native_getCodecCount，持有 Java 层 MediaCodecList 锁。
- 设备运行树原有 tombstoned.rc 使用默认 class；源码计划把 API 26 及以上默认类附加服务列为按需，日志中未见它自动启动。本轮仅通过 Guest 既有 ctl.start 临时启动 tombstoned，私有运行树三个 socket 出现；再次点击 ABC 后新 ANR 文件取得输入法 native 主线程栈，也显示 SoundPool.release 等待。测试后通过 ctl.stop 停止该 Guest 服务，进程快照未见对应 Guest 进程。未修改启动计划、原始固件、宿主安全策略或真实硬件节点；生成的设备 ANR 和 Guest 日志保留在设备，未加入 Git。
- 这些结果定位了本次主线程等待链，尚未定位 MediaCodec 列表锁持有者或阻塞原因，不能认定音频 HAL、解码器或宿主桥为根因。设备还观察到其他 Guest 进程反复 ANR，关联未证实。未修改产品源码、构建或安装；文字输入仍未成功，模块未通过。下一步需在同一失败窗口获取持锁线程和 MediaCodec 服务边界证据，再建立失败测试和最小修复。
- 临时服务停止后，三个 Guest 私有 socket 的文件名仍在运行树中；本轮未手动删除，无法据此断定仍有监听者。Guest 继续运行在测试界面。

### 0.3.0 输入法 MediaCodec 等待边界诊断（2026-11-05，未完成）

- 复查输入法失败链，并在同一独立 clone Guest 中重开搜索框和键盘。点击 ABC 后输入法主进程在后续采样前退出；点击后未取得同一进程的实时等待快照，不能用此前采样冒充点击后的状态。
- 另一次重开键盘但尚未按键时，确认属于测试包 UID 的输入法 PID 19577：两条 SoundDecoder 在 futex 等待；fin-1-thread-4 的 TID 19843 在 ARM64 syscall 212（recvmsg）等待，其 fd 指向 socket。对该同 UID 进程受控发一次 SIGQUIT；同轮 ART 转储确认该 TID 正在 MediaCodecList.native_getCodecCount / initCodecList 并持有 Java 列表锁。此前把 syscall 212 称作 recvfrom 是错误，本记录更正为 recvmsg；该证据不证明谁持有 C++ 互斥锁，也不证明 socket 对端。
- 普通应用 UID 只读 fdinfo 仅得到 socket inode 和标志；/proc/net/unix 与 socket netlink 请求均返回 Permission denied，未提升权限或绕过。引擎 bclient 确实使用 recvmsg 等待 Binder 模拟连接，但不能仅凭 syscall 号认定本次 fd 为 Binder。Guest 媒体进程存在，软件 Codec2 store 已注册；原厂组件注册和 APEX 库加载错误与本故障因果尚未证明。
- 本轮未找到对端或阻塞根因，没有修改产品源码、构建或安装，文字输入仍未通过。生成的 Guest 线程转储和已有运行日志保留在设备，未加入 Git；Guest 保持运行。下一步需要在当前失败窗口以应用私有、默认关闭的受限诊断识别该 recvmsg 连接及对应事务/回复，或取得等价可验证证据，再做失败测试与最小修复；不得靠放宽宿主权限或伪报成功处理。

### 0.3.0 输入法 Binder 客户端受限诊断（2026-11-05，未完成）

- 为确认 MediaCodec 列表线程的等待类型，在 API 36 ARM64 引擎增加默认关闭的诊断：启动前须在该实例私有 run 目录预建属主为应用 UID、0600、单硬链接普通文件 binder.client.trace；限定原始进程名 com.baidu.input_oem 和线程 fin-1-thread-4，只输出 pid、tid、fd、主/hw/vnd 驱动类别及 wait/recv/error，不读取 Parcel、输入内容或路径。recv 仅表示收到完整消息头，不证明事务回复。每进程最多 128 条，文件接近 64 KiB 时停止写；并发检查和写入不是原子操作，不能保证文件大小严格上限。未设置文件时启动探测一次，Binder 读取热路径不做诊断文件 I/O；删除文件停止写入，已启用进程仍会做身份/线程检查至重启。诊断只记录实际命中的事务，启用诊断会影响时序。
- 目标身份、线程、截断、固定格式与单线程条数限制测试先失败后通过；诊断字段由 `reply` 改为 `recv`，对应断言也通过回归，最终测试在手机 ARM64 重新通过。NDK 重建 API 36 引擎，标准和 clone Debug 构建成功；未运行完整单测，未验证并发上限、文件替换竞态、fork/重启边界或真实通信转发的全面等价性。
- Guest 正常停止后为独立 clone 创建临时诊断文件并覆盖安装；最终设备 APK 摘要与本机构建一致。启动后输入法 PID 29896 / TID 30263 的诊断记录显示主 Binder fd 264 有收到消息及最后一次等待；同窗口同 UID 只读 syscall 为 ARM64 212、fd 264，SIGQUIT 转储确认该线程在 MediaCodecList.native_getCodecCount / initCodecList 并持有 Java 列表锁。点击 ABC 后输入法主进程退出；尚未识别目标服务或未返回的事务，也未证明该线程持有 SoundDecoder 等待的原生锁。
- 临时诊断文件已删除；运行中进程的诊断身份检查需重启后才完全关闭。未改原始固件、宿主权限或真实硬件节点，设备原始线程转储未加入 Git。文字输入仍未通过；下一步仅针对已确认的 Guest 主 Binder 通路，限制并核对目标事务及服务的发送/返回，再做根因修复和回归。
- 回滚本次诊断仅移除 bclient.c 的诊断调用及状态、run64.c 的 bct_init 调用、binder_client_trace.h 与其测试，并重建 API 36 引擎和 APK；保留先前 self_cmdline 修正和其他未提交内容。不要删除 Guest 数据或恢复整个工作树。

### 0.3.0 输入法主 Binder 事务定位（2026-11-05，未完成）

- 当前操作和预期：启动 Guest，打开设置搜索框并点 ABC，应输入字符且键盘保持响应。此次测试再次打开键盘，点击 ABC 后键盘消失、搜索框为空，输入法 PID 26385 退出；文字输入未通过。之前多次启动验证见本文件历史记录；本次重启到桌面仅是一次复现，不单独证明稳定启动。
- 为追踪已定位的等待，在 API 36 主 binderd 增加默认关闭诊断：实例私有 run/binder.txn.trace 在 binderd 启动时存在才启用，仅匹配 com.baidu.input_oem 的 fin-1-thread-4 同步事务；仅记录编号、收发阶段、进程和线程号、handle、事务码及标志/错误，不记录 Parcel 或输入内容。最多记录 256 条事件，截断后不再观察后续事务；按进程与线程名筛选、只跟踪同步事务和目标解析后的路径，未命中不能排除其他路径。排队与 sendmsg 成功分别记录，发送成功只表示内核接受消息，不证明目标已经处理；启用可能改变时序。删除标记须重启 binderd 才完全关闭。
- 相关 ARM64 筛选测试先失败后通过；在手机上再次运行筛选及实际 binderd 分发测试，覆盖默认关闭、非目标线程、请求和回复数据原样转发、发送失败，均退出 0；未证明全部 Binder 通信等价。NDK 重建 API 36 binderd，Gradle 标准版单测以及标准版、clone Debug 构建成功（部分任务为 UP-TO-DATE），构建提示 binderd 未 strip、按原样打包。覆盖安装的设备 clone APK SHA-256 为 ca9951029b4268aea275d7f5bfdf2e17a55a483e9228d2e867c8c2be9115b2d5，与本机一致。
- 启用本轮及已有客户端诊断后，输入法 PID 26385 / TID 26612 的事务 id 12、handle 67、code 7 已由主 binderd 发至 Guest /system/bin/mediaserver PID 15258 的 Binder 线程 TID 21295；前 11 笔有 reply-sent，id 12 在按键前后采样中均未见 reply，客户端记录最终为主 Binder wait，未达到事务记录上限。目标线程采样为 futex_wait_queue；事务码本身不足以确认接口和方法，客户端 wait 也不独立证明对应 id 12。按键前同轮 SIGQUIT 的 Guest main 转储确认 TID 26612 在 MediaCodecList.native_getCodecCount / initCodecList 并持有 Java 对象锁，native 部分地址未解析；这仍不证明该线程持有 SoundDecoder 等待的 C++ 锁。
- 测试后已正常停止 Guest，确认 VM 进程退出并删除两个临时诊断标记，事务元数据留在设备应用私有日志，原始日志未加入 Git；原始固件、宿主权限与真实硬件节点未改。未修复输入法卡住，不将本模块或阶段标为通过；下一步只定位 mediaserver 中该请求的接口和工作线程等待链，建立失败验证后再做最小修复。
- 回滚本轮仅移除 GuestVm.kt 的 binder.txn.trace 环境开关、binderd.c 的事务诊断关联和输出、binder_txn_trace.h 及两项新增测试，并按已有构建流程重建 API 36 binderd 和 APK；保留之前的客户端诊断、显示诊断及其他未提交更改。

### 0.3.0 输入法媒体事务接口定位（2026-11-05，未完成）

- 验收操作：启动 Guest、在设置搜索框打开键盘并点击 ABC，预期字符出现且键盘保持响应；本轮再次出现键盘消失、搜索框为空、输入法 PID 694 退出，文字输入仍未通过。已往启动阶段通过的范围仍以兼容矩阵为准，本轮到锁屏不单独证明稳定启动。
- 只读检查指定 Guest 固件：libmedia.so 含 android.media.IMediaPlayerService 常量，BnMediaPlayerService::onTransact 的事务码 7 路径检查接口并调用虚表条目；libmediaplayerservice.so 该条目指向 MediaPlayerService::getCodecList，后者转至 MediaCodecList::getLocalInstance。这是静态路径证据，不证明服务端实际执行到哪一步或持锁情况。
- 基于上轮默认关闭的 binder.txn.trace，仅对已匹配的输入法线程同步事务 code 7、且可安全读取的 inline 数据比较固定接口标识；只记录固定分类 1=不可读取或格式无效、2=其他、3=匹配，不输出 token、Parcel 或输入内容。截断、无效格式及 F_BIG 共用内存均为不可读取；关闭诊断、不匹配线程或其他事务码不做此比较。每次分类占用现有 256 条事件限额的一条，可能改变时序，未命中不能排除其他路径。
- 测试先观察到新分类测试失败（exit 1）和目标分发测试失败（exit 134），实现后手机 ARM64 上重跑分类测试和真实 binderd 分发模式 0–9 均 exit 0，覆盖关闭、非目标线程、匹配/其他/截断、F_BIG、发送失败及请求回复内容不变；其他既有相关测试亦由实施者重跑通过，测试本身不证明全部 Binder 行为等价。NDK 按仓库构建参数重建并 strip API 36 binderd；标准版和 clone Debug 构建成功（多数任务 UP-TO-DATE），Gradle 仍提示 binderd 无法 strip 并按原样打包；测试编译有未使用静态函数警告，未运行完整 Gradle 单测。覆盖安装后设备 APK 与本机 SHA-256 一致：102412f4e4d053e62fcbe6b160adc8c28c2037a70cebc6955a24ff1b8a46d9c9。
- 同轮输入法 PID 694 / TID 1294 的事务 id 12、handle 67、code 7 接口分类为匹配，已发送到 Guest /system/bin/mediaserver PID 16152 / Binder TID 22340，按键前后采样未见该 id 回复，未达到记录上限；客户端最后记录为主 Binder wait，ANR 内核等待快照为 __skb_wait_for_more_packets。目标线程快照为 futex_wait_queue；send 成功不证明服务端已执行，两个等待快照不单独证明同一锁或因果关系。本轮发送过 SIGQUIT，但未找到对应 Java 栈，不能把前轮的栈当成本轮直接证据。
- Guest 已正常停止，VM 进程退出，两个本轮创建的诊断标记已删除；诊断日志只留在设备应用私有目录，原始日志未新增到 Git。未修改原始固件、宿主权限或真实硬件节点；未修复文字输入，阶段不标为通过。下一步针对该 mediaserver 工作线程安全获取同轮服务端栈或等价等待链，先确认普通同 UID 调试能力及其限制，拒绝时不提升权限绕过。
- 回滚本轮仅移除 binderd.c 的 request-token 调用及 binder_txn_trace.h 的固定标识比较，恢复此次 binder_txn_dispatch_test.c 的新增覆盖并移除 binder_txn_token_test.c，重建 API 36 binderd 和 APK；保留上轮事务诊断及其他既有未提交内容。

### 0.3.0 输入法媒体服务工作线程栈诊断（2026-11-05，未完成）

- 目标仍是设置搜索框中点 ABC 后正常输入；本轮设备继续复现键盘消失、搜索框为空，输入法 PID 32231 退出，未修复或通过验收。使用已安装的 clone 测试包，本机与设备 APK 摘要仍是 102412f4e4d053e62fcbe6b160adc8c28c2037a70cebc6955a24ff1b8a46d9c9；本轮未修改产品源码、未重新构建或安装 APK。
- 先在 clone UID 下启动与调试器非父子关系的一次性测试进程：同 UID ptrace 附加/分离成功；LLDB 可展开自造 inner、outer、main 栈并分离，测试目标 TracerPid=0 且继续睡眠。故意传入无效线程时，普通批处理命令提前退出并造成一次性目标消失；随后仅用临时 LLDB Python try/finally 显式分离，正常和错误两次均在一次性目标上确认存活、TracerPid=0。禁用停止时反汇编，设置 SIGSYS 透传；这些测试不保证异常断线、进程被强杀或真实 Guest 的表现。
- 真实复现中，输入法 PID 32231 / TID 32623 的 id 12、code 7、接口标识匹配的请求已发送至同 UID Guest mediaserver PID 16699 / Binder TID 22816；点 ABC 前后均未见对应回复。先核对 cmdline、UID、线程名和 TracerPid，再限定目标线程采样；LLDB 返回 8 帧，但模块和函数均未解析，不能识别服务端调用栈。该采样会暂停进程并可能改变时序。
- 实机分离返回成功后 PID 16699 仍为 T（stopped），与一次性测试表现不同；通过同 UID SIGCONT 后复核目标为 State=S、TracerPid=0，目标线程仍在 futex_wait_queue。调试器已退出，临时 adb forward、应用私有 lldb-server 和本机临时脚本均已删除。不能把真实分离过程称作自动恢复成功。
- 同轮只读两次 /proc 采样显示目标线程处于 ARM64 futex 系统调用；对应 PC 文件偏移位于 Guest libc.so 可执行 PT_LOAD，并落在有长度的 syscall@@LIBC+0x24 范围。进程映射与系统调用为分次快照，仅证明采样时位置，不能确定调用它的上层函数、等待对象、锁持有者或与未回复事务的因果关系；固件、寄存器、完整 maps 和原始日志未写入 Git。
- 测试后正常停止 Guest，确认 VM 进程退出并删除本轮两个临时诊断标记；未修改原始固件、宿主权限或真实硬件节点。文字输入和当前阶段仍未通过。下一步应先在隔离场景验证 Guest 模块识别与栈符号化、以及真实 Guest 分离后的恢复策略，再对目标做最小采样；不得以未知帧猜测根因。

### 0.3.0 Guest 栈识别与分离恢复隔离验证（2026-11-05，未完成）

- 上轮真实 mediaserver 采样的 8 帧模块与函数未解析，LLDB 报告分离成功后进程仍 stopped。本轮只排查这两个诊断失败，未修改产品源码、构建或安装 APK，也没有启动或附加业务 Guest；手机 clone 测试包摘要仍为 102412f4e4d053e62fcbe6b160adc8c28c2037a70cebc6955a24ff1b8a46d9c9。文字输入既有失败未修复，模块和阶段仍未通过。
- 分离恢复隔离测试使用测试包同 UID、自造两线程进程（含 futex 等待），在 LLDB 附加期间人为对该进程发送 SIGSTOP：正常采样和故意触发采样错误两种情况均出现 detach 成功、State=T、TracerPid=0。针对该受控场景，临时工具核对 UID、PID、启动 tick、State 和 TracerPid，并在明确提供采样前运行基线后经 pidfd_send_signal(SIGCONT) 恢复，两种情况均观察到 State=S、TracerPid=0；错误启动 tick、仍被跟踪、已运行及未提供运行基线时拒绝恢复。该基线由调用者提供，状态核查与发信号仍有竞争；人为注入停止仅复现相同现象，不证明真实 Guest 上轮停止原因，也不构成通用自动恢复方案。两次 lldb-server 退出码为 1，报告输出流 Bad file descriptor；虽分离和恢复状态有单独验证，服务自身退出未通过。
- 栈识别使用完全独立空 root、自造带符号静态 ARM64 ELF，经现有引擎启动；实机映射证实该 ELF 被映射，进程 exe 仍为引擎，与上轮模块未识别的假设一致，但未证明因果。首次同 UID 连接/取帧超过 60 秒未返回；核验身份后仅终止一次性合成目标，调试器退出，属非正常分离，终止后出现的未知帧不能作为有效对比。
- 唯一一次重试在连接、取帧、模块加载前后加入 flush 阶段标记并设 25 秒硬超时；PowerShell 后台作业直到合成目标终止后才交付阶段输出，故无法确定超时前实际卡点，也不能把终止后的一帧当作符号化通过。仅终止已核对的本轮合成目标，调试器退出，属非正常分离。未证明手动注册模块可修复真实 Guest 符号识别。
- 手机侧本轮专用测试进程、应用私有测试文件、中转文件和 adb forward 均已清理并复核，业务 VM 仍停止，原有诊断标记不存在；本机仅保留自造测试源码、ELF 和临时调试脚本供复核，未保存真实固件、寄存器或原始个人日志。未提升调试权限、修改宿主安全策略或接触宿主特权节点。下一步先修正隔离测试的输出交付与调试连接可观测性，并验证可靠分离及 Guest 帧符号化，再考虑重新采样真实服务端。

### 0.3.0 连接与取帧两种假设的实机检查（2026-11-06，未完成）

- 按两种假设分别在同一台手机上测试。独立空 root 的合成 Guest 中，lldb-server 已附加、电脑到手机 TCP 已建立；以独立文件实时记录阶段，在目标仍存活的 25 秒窗口内 LLDB 的 gdb-remote 进入后未返回。目标终止后才出现的连接/帧输出不算成功。另一可连接的同 UID 原生对照中，GetNumFrames 约 32ms 返回 5 帧，逐帧读取完成并识别出自造函数；故本次合成 Guest 的卡点在连接调用内部，尚未进入取帧。
- 同一合成 Guest 改由直接 GDB 远程协议请求时，基础握手、线程选择、PC/FP 与有限帧记录读取、D 分离均成功，分离后目标仍存活、State=S、TracerPid=0。进一步用同 UID 私有 socketpair 和 lldb-server --fd 经 adb shell -T 通信，不为调试器开放设备 TCP 监听；隔离测试成功识别自造函数并正常分离。隔离恢复/错误路径测试覆盖采样中抛错时的分离；协议超时/坏校验的本机单测只证明不会误报分离，未证明真实进程异常断线恢复。
- 本轮实际启动 clone Guest，并在设置搜索框打开原厂键盘：输入法 PID 29464 / TID 29796 的事务 id 12、code 7、接口标识匹配，请求发送至同 UID mediaserver PID 9279 / Binder TID 19836，采样前后未见回复。按 ABC 后键盘消失、搜索框为空，输入法 PID 29464 退出；文字输入仍未通过。
- 在核对同轮事务、进程和线程身份、UID、启动 tick 以及私有 tracer 身份后，采用上述私有 FD 通路三次采样目标线程；每次 D 分离确认成功、TracerPid=0，采样工具移除，最终目标仍 State=S。仅输出白名单模块名与文件偏移；有限帧链中的 PC 落于 Guest libc.so 文件偏移 0x98460，7 个返回值按原值均不能映射且高位非零。临时扩充相关媒体库白名单后仍不能映射；低 48 位候选也未命中白名单可执行映射。该候选仅为启发式。原始地址、寄存器、帧内容和未脱敏日志未写入 Git。
- 受限临时采样脚本和自造测试脚本保留在本机系统临时目录供复核，未新增产品源码；本轮未重新构建或安装 APK。测试后 Guest 正常停止、VM 进程退出，私有工具、诊断标记和 adb forward 已清理；未改原始固件、宿主权限或安全策略。下一步应验证远程寄存器编号、栈帧编码和信号帧处理（先用可控合成 Guest 对照），再解释真实服务的未知返回值；不将本模块或阶段标为通过。

### 0.3.0 合成 Guest 普通栈帧对照（2026-11-06，未完成）

- 连接无线 ADB 设备；在独立空 Root 中通过已安装 clone 的引擎运行自造 ARM64 探针，使用同 UID 私有 socketpair 和 lldb-server 的远程协议采样，未启动或附加业务 Guest。实机重复对照退出码 0：g 寄存器块与 p20、p1d、p1f 分别读取的 PC、FP、SP 一致；PC 位于已知 `probe_inner`，p1e 的 LR 位于 `probe_outer`，FP 记录中的返回地址位于 `_start`，与探针已知函数范围一致。响应包检查了校验和。p1e 未与 g 块对照；该结果仅适用于此探针的普通调用链。
- 重复对照的 D 分离返回 OK，清理前目标 State=S、TracerPid=0；随后仅终止自造目标并清理本轮设备私有目录和工具。临时脚本及自造文件留在本机系统临时目录，未新增产品源码，未构建或安装。未验证异常断连恢复、信号帧、真实媒体服务的未知返回值，也未修复文字输入；模块和 0.3.0 阶段仍未通过。下一步应在独立合成 Guest 中验证受控信号帧及其恢复路径，再判断能否安全解释真实服务采样。

### 0.3.0 合成 Guest 信号帧对照（2026-11-06，未完成）

- 已连接设备，在独立空 Root 中经已安装 clone 引擎运行自造 ARM64 探针，使用 `SA_SIGINFO` 处理函数保存中断上下文，再以同 UID 私有 socketpair 和调试协议采样；未启动或附加业务 Guest。首次试验进入处理函数，但错误假设保存的 PC 必在 `raise` 内，断言失败，未完成正常 D 分离；只终止自造目标并清理。依据自造 ELF 符号重新核实，保存的 PC 位于 `tgkill` 范围，不是原假设的 `raise`。
- 修正对照后两次采样均观察到 D 返回 OK，分离后目标 State=S、TracerPid=0，处理函数随后输出返回标记；最后一次脚本退出码 0。该次保存的 FP 出现在七帧简单 FP 链中，保存的中断 PC 未作为返回地址出现，链后段有高位非零的返回值；跨信号帧的展开结果和高位值来源仍不明确。所测仅为自造探针的受控信号及正常分离，不证明异常断连恢复或真实 Guest 的信号帧行为。
- 测试进程与本轮设备私有目录已清理；自造探针、临时脚本及输出保留在本机临时目录，未将地址或原始日志加入 Git。本轮未修改产品源码、构建或安装，未修复文字输入，模块和 0.3.0 阶段仍未通过。下一步先在合成探针中验证如何依据保存的上下文可靠跨越信号边界，及高位返回值的解释，再考虑真实服务采样。

### 0.3.0 合成 Guest 返回地址认证对照（2026-11-06，未完成）

- 在连接的手机上，经已安装 clone 引擎于独立空 root 执行自造 ARM64 静态探针，对比已知未签名地址、PACIASP 签名地址、XPACI 处理结果及同一 SP 下 AUTIASP 认证结果。重复实机执行中，未签名值、XPACI 后的已知签名和未签名值、认证后值均与各自预期一致，探针及运行脚本退出码 0；签名后的原值发生变化。XPACI 只去除认证码，并不验证地址真伪；认证对照只覆盖本次已知、未篡改且 SP 匹配的样本。
- 先前对照中固定低 48 位掩码未恢复已知地址，签名位可影响该范围，不应据此通用截取。上轮信号探针两个高位返回值的低 32 位与自造 ELF 已知调用返回位置一致，相关函数含 PACIASP，仅为静态一致性线索。本轮初次启动命令出错退出 127，修正后完成对照；未启动或附加业务 Guest。
- 设备私有测试目录已清理，自造源码、二进制及临时脚本保留在本机临时目录；未新增产品源码、构建或安装 APK。跨信号边界展开、异常断连、真实服务调用链及文字输入仍未验证或修复，模块与 0.3.0 阶段未通过。下一步在独立合成 Guest 验证依据保存上下文跨越信号边界的受限方法；不能把去码当作认证或直接套用真实地址。

### 0.3.0 媒体服务等待链与受限事务诊断（2026-11-06，进行中）

- Root 和 Guest ADB 已完成授权；本轮使用 Root 完成必要界面输入、少量只读检查，并优先通过应用已有私有 Guest shell 查询，未启用网络 Guest ADB 或修改宿主安全策略。实机多次复现设置搜索框按 ABC 后键盘消失、输入法退出，媒体请求未见回复。
- 同一故障窗口六个 mediaserver 线程等待同一 futex，其映射设备号和 inode 与运行树 `libstagefright.so` 一致，偏移对应 `MediaCodecList::InstanceCache::Get()::sCache` 起始；只读状态位分类为普通锁。运行中 APEX libc 与静态分析文件摘要一致，普通锁路径不记录 owner；静态分析显示持锁路径构建 codec 列表并可能调用 Codec2 服务，尚未证明具体阻塞处。
- 新增默认关闭、按主/hw 分别启用的 mediaserver 同步 Binder 事务诊断，仅记录固定事务元数据、不记录请求内容；各通道 4096 条后提示截断，未解析目标、单向及其他通道不可见。ARM64 身份、限额、真实分发、交错回复与既有事务回归通过，标准版单测及标准/clone Debug 构建通过。先前 256 条版本的 hw 记录在故障前截断；扩大后一次实机中主 Binder 19 笔和 hw Binder 174 笔均记录到 reply-sent，后者持续向 hwservicemanager 发送 code 1；发送回复不证明服务有效或请求完成。
- Guest 私有 shell 可见软件 Codec2 HIDL 服务项，厂商视频 Codec2 本轮发生 SIGSEGV 并显示 stopped；运行树停放的厂商清单声明的是 AIDL 服务，不能直接等同上述 HIDL 查询或认定因果。静态代码表明 hwservicemanager code 1 与 IServiceManager::get 路径一致，但当次接口及服务名尚未确认。为验证查询目标新增默认关闭的受限固定分类，仅处理精确身份和同步请求，无法确认布局、截断或 F_BIG 时保守标为不可读；ARM64 解析和分发测试通过，分类诊断版正在实机复现。
- 合成 Guest 对照验证目标 tracer 可读取 PAC mask、受控去码和正常分离；异常断连和完整信号处理仍未证明安全，不能将该原型用于业务。系统 debuggerd 在隔离 Guest 上失败且目标随后消失，不用于业务采样。各轮已停止的 Guest 均正常关闭并清理标记；本条记录时新一轮测试正在准备，最终清理及结果待补。输入法未修复、0.3.0 未通过。

### 0.3.0 输入法按键退出修复与实机验收（2026-11-06）

- 9008 包的 images 目录中，12 份 XML 的 66 个不同非空文件引用均存在；原始 super 的 LP 元数据校验通过，七个非空 a 分区为 odm、product、system、system_dlkm、system_ext、vendor、vendor_dlkm。原始 vendor_a 中四个 OMX 相关库与运行树同名文件摘要一致；这是局部对照，不证明整个包的来源或完整性。原始 vendor_a 不含 `etc/vintf/manifest.xml`；运行树该清单与项目 `n64-36-own` 资产一致，不能将其中 OMX 声明认作原厂声明。
- 逐笔受限分类实机复现：输入法媒体请求未见回复，同窗口 145 笔已分类 hw 请求均为 OMX/default，持续约每秒一笔；hw 回复已发送不证明取得有效服务。Guest `lshal` 未列出 OMX，目标 `OmxInfoBuilder` 的静态路径调用 `IOmxStore::getService(default, false)`，内部存在等待重试路径。运行树及 API 36 引擎资产按名称未找到对应 OMX 服务程序/rc；不能排除其他命名实现。证据支持项目清单声明与此运行环境不符，而非证明缺少某一镜像。
- 从 n64-36-own 的 `vendor#etc#vintf#manifest.xml` 和 `vendor#manifest.xml` 各移除唯一 OMX HAL 声明块，其他字节保持不变；影响使用该资产的 API 36 ownVendor 实例，不提供 OMX 服务。新增定向测试先失败后通过，标准 Debug 单元测试、标准及 clone Debug 构建成功。诊断的 ARM64 身份、解析、转发、截断与交错回复测试通过；真实短包整体分发安全未验证，既有 do_txn 在分类前复制数据的边界另待检查。
- 覆盖安装 clone APK（SHA-256 `907f85768d70bc443d610bdbaf6b11aab5cf2ac4d9df85040d877eeda7b23bc5`，设备与本机一致）后，运行树两份 manifest 均为新资产摘要 `f67cb2ab7e332e8ab89170d1484bf4474e24206c7c2fb2420b126484fcf28d18`；原运行树两份旧清单被停放，摘要均为 `8ace475b2012347778a6ee2202d740f3a528bcbd266f852449795e847b47672b`，它们是旧引擎资产而非已证明原厂文件。实际搜索框按 ABC、DEF 后键盘保持响应，选词输入“测”，再次输入选词后显示“测啊”并刷新搜索，输入法 PID 持续存活；先前未回复的 code 7 事务本轮有 reply-queued 和 reply-sent，短时 hw 观察未出现此前每秒 OMX 查询。按键音效设置和实际声音播放未核实；长期稳定性及其他媒体功能未验证，不能把 0.3.0 阶段标为通过。
- Guest 已正常停止，四项临时诊断标记移除；未改原始固件或宿主安全策略。回滚需仅恢复上述两份资产至旧摘要（本机临时备份 `rme_omx_manifest_original_1106` 或核对 Git 原版），重建覆盖安装，在 Guest 停止后重新启动并核对两份运行树文件；不要盲目覆盖或删除 parked 数据。默认关闭的诊断代码与对应测试仍保留。下一步验证按键音效启用及实际声音，并开展媒体功能回归；单独审查真实短包分发边界。

### 0.3.0 存储与 APK 安装（2026-11-06，待实机验收）

- 启动阶段既有实测范围：指定设备全新导入后核心进程约 6 分钟、覆盖安装后约 3 分钟观察正常；输入法修复后的基本显示、单点和选词操作见上一节，按键音效不在本轮范围。
- 存储权限回归先通过系统界面授予 clone 管理所有文件权限，系统 appops 显示允许。当前 Guest `sm list-volumes all` 显示 private 和 emulated 均 mounted；Guest 写入自造测试文件后宿主共享目录读回一致，宿主 root 新建测试文件后 Guest 读回一致。宿主调试 run-as 写共享目录曾返回 Permission denied，不能以其推断正常应用行为。测试使用隔离文件；此结果是宿主目录路径桥接，不证明原厂存储 HAL 完整兼容。
- 通过 Guest 文件管理器安装自造包；包管理器确认包存在。RedMagicEMU 自身安装入口返回错误 `Unable to open file: /storage/sdcard/aemu-install.apk`；同轮复现同文件在 `/storage/sdcard` 不可访问、在 `/sdcard` 摘要一致且 `pm install` 成功。实例配置的 `sdcardPath` 为前者，API36 引擎映射后者。测试 Activity 标题出现但内容空白。
- API 36 安装入口不启用监听所有网络接口且无认证的现有 Guest ADB，改为应用私有唯一缓存文件暂存，使用 Guest 包管理器 `pm install -S` 从标准输入安装（复用 ADB 安装语义，不经过 ADB 协议）；检查大小、进程退出及明确 Success，并限制并发、记录清理失败。Guest 内从文件重定向到 `pm install -S` 的自造测试首次及覆盖安装成功；这不证明新入口的宿主到 Guest 输入重定向已实测。旧安装路径的 `seedOwners` 会递归改变已有文件权限，新路径未盲目调用；新装应用私有数据读写须实测。
- 相关定向测试先失败后通过，完整标准 Debug 单测与标准/clone Debug 构建成功；本地模拟测试覆盖空/短读、大小上限、退出、超时、长输出、并发门闩及停止重启后拒绝旧安装等，不能替代真实设备测试。clone APK SHA-256 `c01c9fb7a8db5bd253fea580ecf1eafff42f760b7a0029930082856caa94d180`，手机已带走，未安装此版本。SAF 来源读取可能阻塞且不受进程超时覆盖；若一直不返回，安装门闩会持续占用；应用被强杀时缓存文件清理不能保证。存储与 APK 安装模块未通过完整验收；下次以自造包实际走入口安装、冷启动并核对私有数据读写及清理，再决定是否通过。未修改宿主安全策略或直通硬件节点。

### 0.3.0 APK 安装入口实机验收（2026-11-06）

- 手机重新连接后覆盖安装上述 clone 构建，设备 APK 摘要与本机一致。启动时界面提示上次运行中断，重试后进入 Guest 锁屏；未清除数据，本次不将该提示解释为已解决的启动问题。
- 通过 RedMagicEMU “安装 APK…”入口选择无网络权限的自造测试包；选择前 Guest 包管理器无该包，选择后出现，安装后的 APK SHA-256 `408c7db160f70fcb8488de8fd3d7f1730112a51a34bb6c06adcc281b601f4eca` 与本机测试包一致。测试 Activity 自身在私有 files 目录写入并读回得到 `WRITE_READ_OK`；对该测试包 force-stop 后重新启动，读到原数据并得到 `READ_EXISTING_OK`。这验证入口、安装和该测试应用自己的私有数据持久性。
- 自造包经核验后卸载成功，本轮创建的宿主临时文件和私有诊断输出已删除，应用私有 cache 未发现 `rme-apk-*`；原有 `aemu-install.apk` 未动，未开启 Guest 网络 ADB。Guest 通过菜单正常停止，`:vm` 进程退出。结合上节共享目录双向测试，存储与 APK 安装的基本操作在该设备实测通过；真实停止中断、文件提供方无限阻塞、长期稳定性、其他应用和原厂存储 HAL 完整兼容仍未验证。

### 0.3.0 单卡模拟基带诊断与失败反馈（2026-11-06，未通过）

- 既有稳定启动仅在一台设备的数分钟窗口实测通过；此前一次上次运行中断提示重试后进入 Guest，原因未明。显示、触摸、文字输入及存储基本操作的证据见前述记录。本轮仅检查模拟基带，目标为单卡虚拟短信和虚拟来电，均必须通过模拟基带，不访问宿主真实移动网络或特权硬件节点。
- 旧版实机用唯一测试内容提交短信，Guest 收件箱存在该条记录，随后按 ID 和正文双重条件清理并复查无记录；该路径可能为直接 provider 回退，不证明经过模拟基带。虚拟来电显示 Failed，未见来电界面。源码显示短信优先走旧式 RIL socket，失败时直接写 provider；来电仅走该 socket。radio 设置开启，但本轮日志未见旧 RIL 连接；不能把日志缺失当作完整连接证明。
- 固件停放的原厂 Radio fragment 声明 AIDL v4 的 config/default 及多接口 slot1/slot2，并非当前有效声明；本次仅按单卡检查 slot1。只读 Guest 探针调用 ServiceManager.isDeclared，config/default 和 modem、sim、network、data、messaging、voice 的 slot1 均为 false；仅表示本次实例当前声明状态，不证明原始固件缺少 HAL。现有旧式 RIL 不能直接接入 Android 16 电话框架；单卡模拟 Radio 服务与短信/来电状态机尚未实现。
- 移除直接写短信 provider 的回退；旧式 RIL 写入失败不再报告成功，失败提示说明部分事件可能已发送，成功只表示提交、不证明送达。来电事件可能部分被 Guest 观察，失败后不假装撤销已发送状态。定向测试先复现断连和部分写失败误报，修复后 6 项通过；完整标准 Debug 单测与标准/clone Debug 构建成功，clone APK SHA-256 `18691f12abbe05e16c47b7a1737fa5087f108d90df3a1690c6f12f6c03f8ae92`，设备安装摘要一致。
- 新包实机以虚拟号码提交短信和来电均显示提交失败，未见来电界面；按唯一测试正文定向查收件箱为 No result found。这仅验证基带不可用时不再走数据库回退，并非短信或来电通过。Guest 正常停止，专用测试内容、探针及临时输出已清理；本轮一次 uiautomator 使用通用 `/sdcard/window.xml`，可能覆盖既有同名文件，未删除该文件。下一步需在隔离测试中建立 slot1 AIDL Radio 服务发现与初始化失败测试，再逐步实现和验证虚拟短信、来电及挂断。

### 0.3.0 单卡模拟 Radio 隔离原型（2026-11-06，未完成）

- 目标 Guest 的 voice/messaging AIDL v4 版本、哈希和事务号经运行时反射与 Android 16 r1 冻结接口对照；接口源码及许可证固定保存用于离线重建，生成的 android.* 类不打包进测试 DEX。新增未注册隔离原型覆盖单卡 config、modem/SIM、voice、messaging 和受限桥接；未接入正常 Guest 启动或正式 slot1，不能用于实际短信或来电。
- 合成测试中，Guest 的 voice 合约、跨进程来电状态回调、messaging 合约和跨进程合成字节/ACK、单卡 config、modem/SIM、network 未注册状态、SIM 合成身份及有限 EF 读取均有通过记录；这些测试不证明有效 SMS PDU 已送达、电话框架订阅就绪、真实 UI 或完整原厂 HAL。离线构建及固定接口逐文件校验通过；后续源码仍可能在不同测试后变更，使用前需重新验证。
- 同 UID 合成 Guest 可通过 `/proc/<pid>/fd/0` 向匿名管道注入，不能将匿名 FD 视为隔离。隔离测试中宿主内核逐帧凭据、pidfd 和 epoch 能拒绝测试的同 UID 子进程、旧会话与旧 pidfd；ACK 状态机和 Java 事件会话分别完成隔离测试，但最新跨语言 socket 闭环尚未实机执行，也未接入产品。不得将这些证据解释为完整安全或最终送达。
- 设备测试暂停：SIM 三项隔离 Guest 合约通过，本轮专用 JAR/脚本/输出/UI dump 已删除；Guest 的 `:vm` 仍在运行，未强制停止。voice 测试有三份应用私有输出名写入前未确认是否存在，可能覆盖同名旧文件，保守保留；其余本轮预检过的专用文件已清理。单卡统一状态、电话框架注册、实际短信/来电及接听挂断仍未完成，模块和阶段不通过。下一步设备恢复后先确认 Guest 状态并正常关闭，再继续认证通道跨语言闭环、共享状态及单卡服务初始化；不能恢复原厂双卡或触碰宿主真实基带。

### 0.3.0 单卡 Radio 声明隔离验证（2026-11-07，未完成）

- 设备恢复后，先通过菜单正常停止此前 Guest。本轮限定在现有 clone 中测试，不创建新实例；仅从 VM 注入虚拟短信与来电，Guest 主动发短信或拨号不支持。模拟地区选用 `46000`、运营商名 `RedMagicEMU`，仅为 Guest 虚拟标识，不连接真实运营商、不读取宿主 SIM。
- 单卡隔离模型增加共享状态门控、虚拟 SIM 的有限记录和 SMS-DELIVER 单段编码；目标 Guest 的 IccUtils 能将合成 EF_SPN 解码为 RedMagicEMU，SmsMessage 能解析固定中文及表情 PDU。认证 launcher、pidfd、Unix socket 凭据与 Guest 会话的跨语言合成测试覆盖提交、拒绝、不确定及重复帧；这些结果不证明电话框架已加载 SIM、短信已送达或来电已显示。当前真实模型仍未 READY/注册，未接产品启动。
- 为确认目标 VINTF 加载，在已停止的 clone 运行副本中，以应用 UID 将固定唯一 token 的合成 `IRadioConfig` 声明原子发布，不修改原始固件或正式 `slot1`；身份、内容与目录已核对。启动后测试名称 `isDeclared=true`，未声明名称 false；单个合成 config 服务在另一 Guest 进程读取 descriptor/version/hash 成功，服务退出后名称不存在。Guest 正常停止后，按记录核对 inode、内容和 owner 再清除本轮声明；重新启动验证同 token `isDeclared=false`，随后 Guest 正常关闭，测试 APK、jar、输出、声明与 journal 均清理。本次是人工受控测试，不证明自动崩溃恢复、正式 Radio 注册或电话框架初始化。
- 前期同轮清理了三个经 UID/PID/start tick/cmdline/cwd 核验的旧 `rme_connect_1106` 自造停止探针；未对其他进程发信号。两次调试输出写入前未预检（`/sdcard/rme_bridge_resume_ui.xml` 与应用私有 `run/shell.out`；另两份私有 trace 输出也未预检），可能覆盖已有同名文件，均保留未删除。本轮停止时 Guest 已正常关闭；短信和来电仍未实现或端到端验收，0.3.0 不通过。下一步继续实现受控单卡服务的初始化、宿主事件接入、实际收信和来电/挂断；保留未完成项，不恢复原厂双卡声明或触碰真实基带。

### 0.3.0 单卡来电断电状态本地回归（2026-11-07，未完成）

- 设备离线时仅检查隔离原型。现有 config 模型为一个逻辑 modem、slot1 对应 logical slot 0；当前只返回一个物理槽，尚未证明目标 Guest 界面会将第二物理槽显示为未插卡。不能以拒绝 slot2 服务代替该实机结果。
- 建立失败测试复现来电存在时模拟无线电关闭后旧来电仍保留；在共享状态上添加失效监听，清理语音来电，并在服务关闭时注销。进一步失败测试复现回调重绑可能漏通知、监听器异常阻断后续监听和电源请求缺回复，以及监听器或响应回调同线程重入断连后可能发送过期 OFF；修复后保证其他监听器仍执行、失败明确可观察，并避免 UNAVAILABLE 后的过期通知。Binder 回调失败无法保证对端收到通知，未以之伪报成功。
- 完整本地离线 Radio 合约和 DEX 构建通过，最终输出 `OFFLINE_RADIO_OK dex_classes=346`；先行测试曾分别观察到上述红灯。Guest-only Binder 测试仅编译，设备未连接，未安装或运行新版本。真实电话框架服务、短信及来电端到端尚未完成；模拟基带及 0.3.0 阶段仍未通过。下次设备可用时需验证断电/重绑效果与第二槽 UI，同时继续单卡服务初始化和事件接入；不读取宿主 SIM、不使用真实移动网络。

### 0.3.0 合成 USIM 服务表本地准备（2026-11-07，未完成）

- 设备离线，本轮只处理未注册原型的 USIM 服务表。对照 Android 16 r1 电话框架源码，SIM 就绪后会读取 `EF_SST/UST`（`0x6F38`，USIM 应用目录 `3F007FFF`）；此前隔离服务对该读取返回不支持。这是源码与本地失败测试证据，尚非目标 Guest 实际初始化失败的归因。
- 增加四字节合成表 `00 00 04 00`，仅声明已提供读取的 SPN 服务，不声明 SIM 短信存储或其他未实现服务；四字节也覆盖框架的特殊短信下载服务位查询。通过现有 SIM 接口按限定 AID 和路径提供文件元数据与只读内容，不接入宿主真实 SIM。SIM 状态仍为 DETECTED、网络未注册，不能投递短信或来电。
- 先行回归在补充服务表前以 `EF_UST metadata failed` 失败；修复后最终完整离线 Radio 测试和隔离 DEX 构建退出码 0，输出 `RADIO_SIM_APPLICATION_OK`、`OFFLINE_RADIO_OK dex_classes=346`；独立的构建脚本回归 6 项通过。测试覆盖文件内容、元数据、状态码与回调、前缀读取、错误 AID/路径、越界、写入拒绝及断连。Guest-only Binder 测试仅编译、未执行；未构建或安装应用 APK。
- 正式服务注册、SIM 就绪和网络注册、Guest 框架实际读取服务表、虚拟短信与来电交付、第二槽界面均未验证；单卡模块和 0.3.0 不通过，亦不构成完整原厂 Radio HAL。下一步继续单卡服务初始化与宿主事件接入，设备回来后在现有 clone 验证，不读取宿主 SIM 或使用真实移动网络。

### 0.3.0 单卡声明本地只读检查（2026-11-07，未完成）

- 本地解析未接入正常启动的隔离 VINTF 声明生成器，与七个模拟 Radio 接口核对：声明共六项，缺少 `android.hardware.radio.data.IRadioData/slot1` v4，没有 `slot2`；检查以退出码 1 明确失败。这是生成器的源码检查。
- 生成器清理逻辑依赖模板内容逐字匹配；直接更改模板会导致旧版中断残留无法通过现有身份核对与清理。设备当前离线，无法确认运行树是否有旧版残留，本轮未修改生成器或设备。WSL 中未发现 gcc 或 clang，未运行原生 C 回归；下一步需先准备本地 C 测试环境，建立新旧模板和中断恢复测试，再实现保留旧格式精确核验的清理兼容。实际单卡服务、短信和来电仍未通过。

### 0.3.0 单卡声明与旧版残留本地回归（2026-11-07，未完成）

- WSL Ubuntu 已安装 GCC 和 libc 开发依赖（GCC 15.2.0，新增 25 个包，约 196 MB，未升级已有软件包），并通过独立 C 程序编译运行及 `dpkg --audit` 检查；此环境仅用于本机 x86_64 测试。
- 未注册的隔离声明生成器增加 `android.hardware.radio.data.IRadioData/slot1` v4，生成结果检查七个接口的包名、版本和实例且没有 slot2。旧六项格式仅供清理时按完整原字节识别，不可作为当前七项声明通过验证；清理仍需原有 token、文件类型与单链接、设备号/inode 记录、目标链接和停机 guard 检查。该机制依赖调用者的独占和停机前提，不能防护同 UID 并发修改。
- 修改前本地 C 测试先因缺少 data 失败；只添加 data 后，独立旧格式 fixture 的清理测试再次失败。完成兼容后 GCC 严格警告编译的测试、ASan/UBSan 版本均输出 `RADIO_VINTF_FRAGMENT_OK` 并退出 0。测试包含旧声明完整和目标缺失的清理、仅 journal 残留、错误 token、停机 guard 变化、内容和 journal 篡改、相同内容的新 inode、额外硬链接及错误目标；不将文件已不存在时的 journal 清理称作 inode 验证。测试全部在临时目录执行。
- 已用本地 NDK 28.2 以 Android API 35/AArch64 目标交叉编译并链接当前源码和测试，核对 ELF 为 AArch64；NDK 缺少 API 36 链接启动文件，API 36 构建及目标运行未验证。未构建或安装应用 APK，未连接手机、发布声明或操作设备文件。声明生成器尚未接入产品启动，VINTF 加载、正式服务注册、SIM 就绪、短信和来电均待实现及实机验证，单卡模块和 0.3.0 不通过。

### 0.3.0 模拟基带初始化状态本地回归（2026-11-07，未完成）

- 未注册的 modem 原型绑定回调时，先前在 `rilConnected` 前保存状态；本地失败测试在该回调中同步断连，观察到先发 `UNAVAILABLE` 后又发旧 `OFF`。改为回调返回并核查回调身份后读取当前状态，避免该测试路径重放旧快照。
- 本地回归同时覆盖回调内开机，以及另一线程在回调期间完成模型断连、随后完成通知的场景；正常初始化、回调死亡与旧有断电回归也包含在完整套件。相同状态可能重复通知；状态读取与其他线程改变状态并非原子操作。电源响应回调重入重新绑定后旧回调仍可能收到通知的源码风险不在本轮验证范围。
- 最终完整本地 Radio 合约测试和隔离 DEX 构建退出码 0，输出 `RADIO_MODEM_SIM_CONTRACT_OK`、`OFFLINE_RADIO_OK dex_classes=354`。Guest-only Binder 测试仅编译未运行，未构建或安装应用 APK，设备未连接。正式服务启动和事件通道尚未接入，SIM 仍未就绪，短信与来电未端到端验证；单卡基带及 0.3.0 仍未通过，不能视作完整原厂 Radio HAL。
- 下一步先复现并处理电源响应期间的回调重绑风险；随后继续隔离事件接入。当前事件协议尚无宿主侧带通话关联标识的挂断事件，不能假设宿主已能安全结束指定虚拟来电。

### 0.3.0 模拟基带电源回调重绑本地回归（2026-11-07，未完成）

- 未注册 modem 原型的本地合成测试在电源响应回调内同步重绑：原请求已向旧响应回调回复，新状态回调完成初始化后，旧状态回调仍收到 ON；先行测试因此失败。修复后，原请求仍回复原回调，发送后续状态前重新核对回调身份和死亡标记；若已重绑或已观察到死亡则不再通知旧回调，新回调由初始化路径回放当前状态。
- 回归覆盖响应内重绑、旧死亡通知不影响新绑定、响应或状态回调在响应中死亡、关电监听器内重绑、响应异常传播和重绑初始化失败。后一用例中的初始化异常由测试回调自行捕获。身份检查与实际发送之间仍可能并发发生 Binder 死亡，不能保证原子性或对端已处理；真实 AIDL 是 oneway，本地同步重入并非目标 Guest 实测。
- 最终完整离线 Radio 合约测试和隔离 DEX 构建退出码 0，输出 `RADIO_MODEM_SIM_CONTRACT_OK`、`OFFLINE_RADIO_OK dex_classes=359`；首次新增测试有变量重名的编译错误，修正后才取得上述失败及通过证据。Guest-only Binder 测试仅编译未执行；未构建或安装应用 APK、设备未连接。正式服务和事件通道尚未接入，SIM 未就绪，虚拟短信、来电及 0.3.0 仍未通过。下一步继续隔离事件接入，先设计带通话关联标识的宿主挂断与延迟事件拒绝；设备恢复连接后验证跨进程和端到端行为。

### 0.3.0 单卡宿主挂断关联本地原型（2026-11-07，未完成）

- 隔离事件协议新增 kind 4，负载固定为原来电帧的非零 epoch 和序号；目标 epoch 必须等于当前会话。kind 2 来电的帧身份与当前通话一起保存，挂断在语音队列和状态锁中按两个字段匹配、清除；Guest 挂断/拒接、断电、当前回调死亡及关闭时同时清除身份，接听与普通回调重绑保留身份。未关联的旧测试入口不会匹配该挂断。认证和每帧序号检查仍由原有受限 socket 通道负责，不访问宿主真实基带。
- 先行本地测试分别复现 native 网关不接受 kind 4、Java 会话不处理挂断及语音关联接口缺失。修复后最终完整离线 Java Radio 合约与隔离 DEX 构建退出码 0，输出 `RADIO_VOICE_ASSOCIATED_HANGUP_OK`、`RADIO_EVENT_SESSION_OK`、`OFFLINE_RADIO_OK dex_classes=370`。测试覆盖同号码新来电不被旧身份挂断、响铃/接听后匹配挂断、Guest 拒接、旧 epoch、错误帧、重放、通知异常与重绑。提交、拒绝和不确定的结果只表示本地处理，不证明 Guest 框架或设备端已收到。
- WSL 本地 GNU C 严格警告构建的 gate、真实 Unix socket/pidfd 接收、ACK 和 ACK wire 四项均通过；socket 测试对挂断帧覆盖同 UID 其他进程拒绝、合法身份接收及重放拒绝。一次使用严格 C11 的构建未提供既有 `clock_gettime` 特性声明而失败，改用 GNU C 模式后完整通过；不是 Guest 跨语言端到端测试。
- 跨会话防误匹配依赖创建方为每个会话分配不重复的非零 epoch，现有构造器/网关不保证跨会话唯一性；新会话不能用旧 epoch 挂断遗留通话，断连清理策略尚未接入验证。队列超时或通知异常可能已改变状态，不能自动按未执行重试。宿主 UI/正式事件通道和服务启动仍未连接，设备未连接，Guest-only Binder 测试仅编译；未验证真实跨语言通路、短信和来电，也未构建或安装应用 APK。单卡基带和 0.3.0 不通过。下一步完善受控会话创建、epoch 生命周期和断连清理，再在现有 clone 上实测。

### 0.3.0 单卡事件会话生命周期本地原型（2026-11-07，未完成）

- 隔离原型新增受控会话管理器：同一语音实例只允许一个事件入口和一个管理器；同一类加载器内各管理器共享非零、不回绕的递增 epoch，旧会话完成清理后才能打开下一会话。认领前通过入口的在途检查和语音队列屏障拒绝未结束的旧事件或旧来电；认领后旧测试入口受限。包内的来源标志仅协调可信代码，不能代替 socket 对端凭据认证。
- 会话关闭先停止新事件，再在语音队列按该会话 epoch 清理通话，旧/伪造会话的关闭须通过来源和活动身份核对；`CLEANED` 仅代表该 epoch 的清理操作完成。清理期间重复关闭可返回 `IN_PROGRESS`；事件仍在处理、排队失败、超时或通知失败时返回 `UNCERTAIN`，保留失败原因并要求管理器不再开新会话。该要求目前只是状态标记，尚无产品调用方强制退出隔离进程。
- 新增隔离传输泵，在合成帧源遇 EOF、读写异常或未检查异常时触发关闭；普通传输错误与清理失败分别保留，不能把进行中/不确定当作清理通过。该泵不提供真实 socket、认证或中断阻塞读取的机制。
- 先行失败测试分别观察到会话管理器、语音关闭和传输泵缺失；复审再复现旧入口绕过接管，以及旧会话用相同 epoch 关闭时误清新来电，均修复并增加回归。最终完整本地 Radio 合约测试及隔离 DEX 构建退出码 0，输出 `RADIO_VOICE_SESSION_CLOSE_OK`、`RADIO_EVENT_SESSION_PUMP_OK`、`RADIO_EVENT_SESSION_OK`、`OFFLINE_RADIO_OK dex_classes=407`。覆盖并发分配、耗尽拒绝、旧会话与伪造来源、队列超时/拒绝、回调异常/重绑、并发关闭、EOF 和传输异常；Guest-only Binder 测试仅编译未执行。
- epoch 不重复仅在当前类加载器生命周期内成立，跨进程重启仍需要可信握手或持久化；宿主尚未获取并协商该标识。真实 socket EOF、身份校验链、进程退出执行、正式服务启动和 UI 尚未接入，设备未连接，未构建或安装应用 APK。单卡模拟基带、虚拟短信和来电及 0.3.0 均未通过实机验收；下一步设计受控宿主握手与退出监督，设备恢复连接后在现有 clone 验证。

### 0.3.0 目标 Guest 单卡 AIDL 启动替换（2026-11-07，待实机）

- 指定 NX809J、Android 16/API36、ARM64、N64 且启用模拟基带时，启动选择改为单卡 AIDL Radio，不再启动旧 RIL；其他情况保留既有行为。运行副本安装独立 JAR 和普通 vendor VINTF 片段，在 servicemanager 后、zygote 前请求运行 Radio 进程。缺少资产或冲突会报错；原始固件未改。正式片段包含 `IRadioConfig/default` 及六个 `slot1`，无 slot2；启用开关关闭时，仅撤销与当前项目资产字节一致的声明。项目 JAR 标记仅为更新约定，不是防伪认证。
- Radio 入口将七个服务绑定到同一合成卡状态，先检查正式名已声明且未占用，注册后观察同名 Binder 存活及 descriptor；这不证明 Binder 身份、版本或电话框架完成初始化，检查和注册也非原子。失败由该进程退出；ServiceManager 调用可能阻塞，GuestVm 异步启动且当前没有针对该服务的自动重启或注册超时，服务失败后已声明的接口可能令框架等待，存在影响启动的风险。旧虚拟短信/来电界面尚未接入 AIDL 通道，现阶段不会因服务启动而可用。
- 本地注册定向测试先因入口缺失失败，补上后通过；再次建立“注册调用返回但服务不可见”的失败用例，加入可见性检查后通过。运行副本安装与目标启动选择有定向回归；完整本地 Radio 合约及隔离构建输出 `RADIO_SLOT1_REGISTRY_OK` 和 `OFFLINE_RADIO_OK dex_classes=416`。服务 JAR 仅含指定服务及其内部类、DEX 和项目标记；本机重建与 asset 字节一致，SHA-256 `b8baf738a99439c312ec186fcc9e3717f1f54b033a29005af86ea5091d526859`。
- 标准 Debug 完整单测 267 项、失败 0、跳过 39；标准和 clone Debug 均构建成功，核对两个 APK 中 Radio JAR 与声明 asset 均与源码树一致。clone APK SHA-256 `95c8aec60a0c313e0afd56886d816ecee08d6841067c7c4597bfe04ebe804601`。设备不在，未覆盖安装、启动或注册；Guest-only Binder 测试仅编译。运行树目录/权限、正式七项 VINTF 加载、服务生命周期、框架识别及稳定启动均待现有 clone 实测。
- 目标形态确定为单卡 **4G/LTE** 虚拟状态；当前 SIM 仍为 DETECTED，网络仍未注册、类型 UNKNOWN，尚未实现 4G 显示、移动数据或 VoLTE，短信和来电未接入。此为模拟 Radio，不访问宿主真实 SIM 或移动基带。0.3.0 不通过。回滚可在宿主关闭模拟基带并重新启动 Guest，使匹配的项目声明移除；若要恢复目标的旧 RIL 启动，需回退对应 `GuestVm`/`TargetRadioLaunch` 替换并重建，保留原始固件和其他用户数据。

### 0.3.0 单卡 AIDL 服务首次实机检查（2026-11-07，未通过）

- 连接设备后确认先前 clone Guest 未运行，在现有实例覆盖安装上一轮 clone Debug APK，设备上 APK 的 SHA-256 与上一轮构建一致；未新建实例或改动原始固件。目标运行副本中的 Radio JAR 和 VINTF 片段与 APK 资产一致。启动日志报告单卡 AIDL 服务启动、Guest 在约 41 秒报告 boot completed；这是单次启动观察，不证明长期稳定。
- 服务自身报告七个正式名称可见；另由独立 Guest 进程的 service list 看到 config/default 和六个 slot1 接口，逐个读取 AIDL 接口版本均为 4，未见 slot2 的对应接口。这确认本次注册及跨进程查询成功。
- Guest 电话框架有槽 0 的记录，但 SIM 状态为 NOT_READY、槽 1 为 UNKNOWN，活动订阅为空；设置的双卡页面中卡 1 和卡 2 均呈灰色。当前源码返回的 SIM 应用状态仍为 DETECTED，与实测 NOT_READY 一致，但本轮没有直接读到框架收到的应用状态。未验证第二槽实际表现为未插卡。
- 测试结束向 Guest 请求关机，调用返回 Binder `Bad file descriptor`；应用随后显示 Guest powered off，`:vm` 进程已退出。未以此认定关机流程完整通过。本轮测试输出已清理，未运行新的构建或修改产品代码。
- SIM 就绪和活动订阅、第二槽未插卡、模拟 4G/LTE、虚拟短信和来电均未通过实机验证，单卡模块及 0.3.0 不能标为通过。下一步追踪电话框架的 SIM 应用及记录初始化，在现有 clone 验证第一槽真正就绪和第二槽状态；不得通过简单伪报 READY 代替初始化或访问宿主真实 SIM。

### 0.3.0 虚拟 USIM 应用初始化（2026-11-07，单卡模块未通过）

- 本轮先在原有 clone 重现：Guest 的 TelephonyDebugService 显示第一槽卡存在、USIM 应用为 APPSTATE_DETECTED，外部状态为 NOT_READY；此前的 RadioSimService 的确固定返回 DETECTED。Guest logcat 因 logd 未运行不可用，采用电话框架自己的诊断输出核实状态；只记录状态，不保存完整输出。
- 虚拟卡已有合成 IMSI、ICCID、AD、SPN 和 UST 的受限读取；将其应用状态改为 READY、PIN1 改为 DISABLED，使无需 PIN 的应用进入框架记录读取流程。应用处于 READY 时，共享模型的 SIM 投递就绪及网络注册门控仍为 false。先行离线测试因应用不是 READY 而失败，修改后完整离线 Radio 测试通过，输出 `RADIO_SIM_APPLICATION_OK` 和 `OFFLINE_RADIO_OK dex_classes=416`；Guest-only Binder 测试仅编译。JAR 构建成功，SHA-256 `1b1ac065594ef719c78adc9f5cd84f725f4b81cb25afcfdcc599e1f13e4f6b22`。
- clone Debug 构建成功并覆盖安装到同一实例，APK SHA-256 `b13a1a2a785e0ba2ccd81c07c4060c9812047dca3d27bffe95fba0993a6c35ab`，设备 APK 与运行副本 JAR 摘要均与本机一致；标准单元测试任务为 UP-TO-DATE，非本轮重新执行。构建脚本回归 7 项中 6 项通过、1 项跳过。
- 设备上的电话框架现报告第一槽应用 APPSTATE_READY、记录请求完成且外部状态 LOADED，建立一条活动订阅；订阅显示名 RedMagicEMU、MCC 460、MNC 00、地区 cn，SPN 显示 RedMagicEMU。IMSI 和 ICCID 在框架诊断中已遮罩，未核对完整值；诊断中仍有 REQUEST_NOT_SUPPORTED，此时可读到 LOADED 状态。第二槽仍为 UNKNOWN，尚未验证界面显示未插卡。
- 测试结束后 Guest 关机请求再次返回 Binder `Bad file descriptor`，应用随后显示 Guest powered off，关闭界面后 `:vm` 进程已退出；不以此认定关机流程完整通过。测试临时输出和截图清理，不读取或直通宿主真实 SIM、基带及硬件节点。恢复时可回退本轮 SIM 应用状态及 PIN 代码与相关测试、重新构建并部署 JAR；保留其他既有修改和原始固件。
- 第一槽框架识别已取得实机证据；第二槽未插卡、4G/LTE 注册、虚拟短信和来电仍未完成或验收，单卡模块和 0.3.0 仍未通过。这是模拟 Radio 服务。下一项先定位第二槽为 UNKNOWN 的原因，并使其表现为未插卡，再在同一 clone 实测。

### 0.3.0 第二物理卡槽状态（2026-11-07，单卡模块未通过）

- 修改前在现有 clone 实测：第一槽 PRESENT、USIM READY、外部状态 LOADED；电话框架预留两个物理槽，但 `mUiccSlots[1]=null`，逻辑 phone 到物理槽的映射为 `[0, -1]`。原厂双卡设置页卡 1 显示 RedMagicEMU，卡 2 灰色。原模型 `getSimSlotsStatus` 只返回第一个物理槽；`isub.mSimState[1]=UNKNOWN` 是第二逻辑电话状态，不能直接当作第二物理槽的卡状态。
- 模拟 config 增加第二个物理槽，返回 ABSENT、一个未激活且逻辑 ID 为 -1 的端口、空卡身份；第一槽及单个逻辑 modem 的服务模型不变，不注册 slot2。测试先因缺少第二物理槽以 `expected two physical slots` 失败；修复后完整离线 Radio 测试通过，输出 `RADIO_CONFIG_SINGLE_SLOT_OK`、`OFFLINE_RADIO_OK dex_classes=416`，Guest-only Binder 测试仅编译。
- JAR 和 clone Debug 构建成功，JAR SHA-256 `94cea9d08f35d8f9a86a96b91614f6a718295a5d17a7eaa52dd5634e173b9d66`，APK SHA-256 `36fc74a9399ba1b5844050a0cdc0db37e941918a49eb3ccc52df47140e5e8efd`；覆盖安装到同一 clone，设备 APK 和运行副本 JAR 摘要与构建一致。标准单测任务为 UP-TO-DATE，非本轮重新执行。
- 设备电话框架现有两个物理槽：第一槽 PRESENT、READY/LOADED，第二槽 `mActive=false`、`CARDSTATE_ABSENT`；映射仍为 `[0, -1]`。第一槽仍有 RedMagicEMU 活动订阅；独立 Guest `service list` 仅见 config/default 和六个 slot1 服务，未见 slot2。设置页卡 1 为 RedMagicEMU、卡 2 灰色，点击卡 2 开关未见变化；页面未明确写“未插卡”。
- `isub.mSimState[1]` 仍为 UNKNOWN，电话框架诊断中的 `mActiveModemCount=2` 与模拟 config 的一个逻辑 modem 模型不一致；框架报告的 modem 数量仍为 2。下一项先追踪框架的 modem 计数和第二逻辑状态，保持第二槽无卡，不新建第二张卡或触碰宿主真实基带。4G/LTE、虚拟短信和来电仍未完成，单卡模块及 0.3.0 不通过。
- 测试结束向 Guest 请求关机，调用返回 Binder `Bad file descriptor`；应用显示 Guest powered off，关闭界面后 `:vm` 已退出，关机流程未完整验收。测试临时输出与截图清理；回滚本轮可恢复单槽返回与相关测试、重建并部署 JAR，不改原始固件及其他既有工作。

### 0.3.0 单 modem 启动配置（2026-11-07，单卡模块未通过）

- 本轮在现有 clone 重现：Guest 属性 `persist.radio.multisim.config=dsda`，电话框架 PhoneSwitcher 报告 `mActiveModemCount=2`，但模拟服务只提供 slot1；第一槽 LOADED，第二物理槽 ABSENT。停止 Guest 后，在预先确认不存在的私有 `run/props.extra` 临时指定该属性为空：实机 PhoneSwitcher 变为 1、映射 `[0]`，第一槽仍 LOADED、第二物理槽仍 ABSENT。试验后确认 Guest 没有产生该属性的持久文件，已删除本次临时配置。
- 启动代码仅在 NX809J/API36/ARM64/N64 且开启目标 AIDL Radio 时，将该 Guest 属性覆盖为空；其他后端不改变。覆盖发生在 `props.extra` 之前，保留既有显式调试覆盖优先级；不修改宿主属性、原始固件或 Guest 持久文件。先行定向测试因缺少启动属性配置而失败；修复后的目标选择、关闭/其他后端不变、无关属性保留以及真实 API 36 属性区从 dsda 覆盖为空的测试通过。
- JDK 21 完整标准单元测试实际执行 269 项、失败 0、跳过 39；标准与 clone Debug 构建成功。clone APK SHA-256 `c37a02a7bb50489b8b0ee87deeac733c7aa172ddba54f691deb4f888f9dd5caa`，设备覆盖安装包摘要一致；运行副本 Radio JAR 保持上一轮摘要 `94cea9d08f35d8f9a86a96b91614f6a718295a5d17a7eaa52dd5634e173b9d66`。
- 清除临时 `props.extra` 后启动正式构建，Guest 多卡属性为空，PhoneSwitcher 报告一个活动 modem，映射 `[0]`；第一槽 PRESENT、READY/LOADED，仍有 RedMagicEMU 活动订阅，第二物理槽未激活且 ABSENT。移动网络页面不再显示“双卡设置”入口；订阅服务的第二预留状态仍为 UNKNOWN，不作为第二张卡的证据。未进行长期稳定性或其他宿主验证。
- 结束时 Guest 关机请求返回 Binder `Bad file descriptor`，应用显示 Guest powered off，关闭界面后 `:vm` 进程已退出；关机流程未完整验收。试验前后均未发现该 Guest 持久属性文件；临时输出和截图已清理。回滚可移除目标启动属性覆盖和相关测试并重新构建；若未来 Guest 自行写入持久属性，回滚需另行检查，不能假设其不存在。
- 单 modem 框架识别已在本设备取得证据；模拟 4G/LTE、虚拟短信和来电尚未完成或端到端验收，单卡模块及 0.3.0 仍不通过。模拟 Radio 不连接宿主真实移动网络；下一项先打通 Guest 的虚拟 LTE 注册状态，再验证框架识别和后续宿主事件接入条件。

### 0.3.0 虚拟 LTE 注册状态（2026-11-07，单卡模块未通过）

- 修改前现有 clone 的电话框架显示 CS/PS 未注册、RAT UNKNOWN，虽然无线电希望开机。补充回归先因开机模拟网络未注册而失败；模拟网络现于连接且无线电 ON 时报告 HOME/LTE、46000 和 RedMagicEMU，OFF/断连返回 RADIO_NOT_AVAILABLE，SIM 事件投递门控仍关闭。完整离线 Radio 回归通过（`RADIO_NETWORK_UNREGISTERED_OK` 为沿用的测试名，现覆盖 LTE、电源和断连；`OFFLINE_RADIO_OK dex_classes=416`），Guest-only Binder 测试仅编译。
- 首个设备构建的 LTE 响应进入 `mNewSS`，但 `mPollingContext=[1]` 持续不归零、RIL 请求列表为空，当前 `mSS` 仍为 POWER_OFF；受控试验仅在 Guest 中临时通过包管理器停用默认状态的 `vendor.qti.iwlan/.IWlanNetworkService` 并重启，框架计数归零，当前 `mSS` 变为 LTE IN_SERVICE。随后已将该组件用户状态恢复为 default，并核对没有显式启用或停用记录；未更改宿主组件或原始固件。
- 正式代码仅在目标 AIDL 模拟基带启用时，为 Guest 运行副本发布独立 sysconfig，将该原厂 IWLAN 网络服务默认设为不可用；关闭模拟基带时仅删除内容相同的项目配置，外来文件冲突保留并报错。测试先因配置缺失等失败，修复后安装、精确清理、冲突与目标选择回归通过；Guest 用户显式组件设置可覆盖此默认值。此兼容措施使 Guest IWLAN／Wi-Fi 通话不可用；关闭模拟基带后的实机恢复尚未验证。
- JDK 21 完整标准单测执行 275 项、失败 0、跳过 40；标准与 clone Debug 构建通过。clone APK SHA-256 `ee488d0295da8927a01d392d7bce8abb5ea6fb50ce4ca4d32f17eceb1fe99b10`，覆盖安装设备摘要一致；Radio JAR SHA-256 `c251a4f75914797b211c8dea32fec330dfa81523984a59f84a343a96dd998844`。
- 正式构建在原组件用户状态为 default 时，Guest 查询不到该网络服务、进程未绑定；电话框架当前 `mSS` 显示 CS/PS HOME、LTE 和 RedMagicEMU，活动 modem 数为 1、第一 SIM LOADED，状态栏出现 4G。Guest 打开飞行模式后 `mSS` 为 POWER_OFF，关闭后恢复 HOME/LTE，待处理计数为 0。此为一次实机往返，不证明长期稳定或其他宿主兼容。
- 虚拟注册会使应用看到在网状态，但实际移动数据承载、IMS/VoLTE、虚拟短信与来电仍不可用，信号强度接口也仍不支持；不访问宿主真实 SIM、移动基带或特权硬件节点。单卡模拟基带及 0.3.0 仍未通过；下一项继续实现宿主事件到 Guest 模拟基带的最小接入与虚拟短信交付。
- 测试后 Guest 关机命令返回 Binder `Bad file descriptor`，应用显示 Guest powered off，关闭界面后 `:vm` 已退出，关机流程未完整验收。已清理本轮诊断文件和截图；回滚时撤销网络状态代码及回归、重建 JAR，并在关闭模拟基带后启动以移除匹配的 sysconfig，保留其他工作和原始固件。

### 0.3.0 正式短信事件端点接线（2026-11-07，未通过）

- 现有宿主虚拟短信入口仍调用旧 RIL，目标 Guest 已改用 AIDL，因此该入口尚不能向正式短信服务投递。Guest 只读诊断显示默认短信应用为 `com.android.messaging`，电话框架有 RadioMessagingProxy、单 modem 和正常 LTE 状态。
- 正式 Radio 服务组现在在发布前构造并持有内部事件端点，与正式注册的 messaging、voice 和单卡模型使用同一组对象；服务 JAR 精确加入现有 endpoint 及其两个 session 依赖。未创建事件会话、socket、监听器或新的 Binder 入口，SIM 投递门控保持关闭。端点会占用该 voice 实例的唯一端点资格，但不改变其对外注册名。
- 先行构建边界及正式服务组测试因缺少所需 bridge 类和服务组入口失败；修复后完整离线 Radio 回归通过（`RADIO_SLOT1_REGISTRY_OK`、`OFFLINE_RADIO_OK dex_classes=417`），覆盖服务对象共享、重复端点拒绝和未就绪 SIM 拒绝短信。构建边界测试 7 项中 6 项通过、1 项跳过；Guest-only Binder 测试仅编译。clone Debug 构建通过，标准单测任务为 UP-TO-DATE，非本轮重新执行。
- clone APK SHA-256 `bcb7f03ff12cc10da7cb2f195ad362a9f6f74d18fce7ffc6f4cbd7f719b3c331`，设备覆盖安装摘要一致；运行副本 JAR SHA-256 `341ce617e50a835089271c7f1c05cee61405396f00b4a7ed9ea80f7bfdc4fcca` 与构建一致。实机新服务进程报告七项正式名称可见，独立 Guest `service list` 可查 config/default 与六个 slot1；SIM 属性仍为 LOADED，框架仍报告一个活动 modem 和 RedMagicEMU LTE。该启动回归不验证短信交付。
- 宿主到端点的认证 socket、进程身份及生命周期接入、合成 SIM 投递就绪、正式回调和短信收件箱端到端均未实现或验证；不能把本地提交当作实际收信，单卡模块及 0.3.0 仍未通过。下一项将现有认证门控与宿主事件传输接到此正式端点，保持不开放未认证 Guest 接口，再实现可验证的短信投递。
- 测试后 Guest 关机调用返回 Binder `Bad file descriptor`，应用显示 Guest powered off，关闭界面后 `:vm` 进程已退出；关机流程未完整验收。测试临时文件和截图清理；回滚可恢复服务组构造与构建白名单、重建并部署 JAR，保留原始固件及其他已有工作。此为模拟 Radio，既不访问宿主真实移动网络，也不证明完整原厂 Radio HAL。

### 0.3.0 Guest 逐帧来源校验与设备诊断（2026-11-07，单卡模块未通过）

- 旧隔离原型对宿主收到的 ACK 做逐帧认证，不能直接证明 Guest 收到的事件来自宿主；目标 N64 还会改写连接级 UID。为后续事件通道增加独立的 Guest 逐帧凭据校验：要求唯一、完整的 `SCM_CREDENTIALS` 且 PID/UID 匹配，拒绝截断和其他附加消息。组件只校验调用方提供的元数据，不负责接收、身份来源认证或关闭收到的文件描述符；未接入正式 socket、事件端点或 SIM 就绪门控。
- 先行测试因校验组件缺失而失败。完成后完整离线 Radio 回归输出 `RADIO_GUEST_PEER_CREDENTIALS_OK`、`OFFLINE_RADIO_OK dex_classes=420`；服务构建边界测试 7 项中 6 项通过、1 项跳过。并行准备设备诊断时曾因实验类暂时位于自动收集目录造成一次离线运行失败，将其移至独立 diagnostics 目录后重跑通过。服务 JAR 单独构建成功，SHA-256 `61bc6db2a72f974ac0973918ae631554c221dabdf149d6a5686e1cbd03284398`；该构建未写入应用资产、未安装 APK，设备仍运行上一轮版本。
- 在现有 clone 内，独立 N64 Guest 进程通过临时抽象 Unix socket 发送合成消息；另一个 Guest 进程以 `SO_PASSCRED` 和 `Os.recvmsg` 接收。同 UID 的发送父进程和 fork 子进程被内核标记为不同 PID，实际接收 UID 为应用物理 UID，发送程序中 Guest 的 `getuid()` 则显示 0。新增校验接受父消息、以父 PID 为预期拒绝子消息，发送程序核对子 PID 后与接收程序均报告通过。这只证明本次 Guest 到 Guest 的隔离试验，不证明真实宿主 UI 到正式 Radio 进程的连接认证、生命周期或端到端投递。
- 起初通过 `run-as` 执行临时设备程序被系统拒绝；改用 shell 用户启动后，Guest 连接 shell socket 又被权限策略拒绝。没有修改系统安全策略，随后改为上述 Guest 内隔离试验。真实接收层如何安全关闭 `SCM_RIGHTS`、截断及异常路径、认证启动进程、跨进程会话握手和就绪状态仍待实现或验证；短信、来电和 0.3.0 均不能标为通过。
- 本次复用已有实例，启动属性报告 boot completed；关机调用返回异常，关闭界面后 `:vm` 已退出，不能认定关机流程完整通过。预检的诊断文件已删除；一次 UI 检查写入 `/sdcard/window-rme-cred-1107.xml` 前未检查是否存在，可能覆盖同名旧文件，现保守保留。回滚可移除本轮独立校验与诊断、恢复服务构建白名单和相应测试；未部署的临时 JAR 不影响当前设备。下一项应把经过身份认证的真实宿主连接与安全接收、描述符清理接入正式端点，再验证虚拟短信交付，不开放未经认证的事件入口。

### 0.3.0 单卡事件投递门控与实机框架试验（2026-11-07，模块未通过）

- 虚拟卡已被电话框架识别，但共享模型 `isSimReady()` 固定返回 false，使正式短信、来电事件端点全部拒绝；这与合成 USIM 应用在连接时返回 READY 的现状不一致。现将模型中的应用可用状态与连接状态一致，仍要求无线电开启和模拟网络注册；实际 messaging/voice 回调未绑定时不会报告提交。
- 先行回归以 `connected synthetic USIM application not ready` 失败；修复后完整离线 Radio 测试通过，包含真实共享模型配合合成回调的短信和来电、未绑定回调、关电、断连及在途状态变化，输出 `RADIO_EVENT_SHARED_STATE_OK`、`RADIO_SLOT1_REGISTRY_OK`、`OFFLINE_RADIO_OK dex_classes=420`。Guest-only Binder 测试仅编译。构建边界 7 项中 6 项通过、1 项跳过；标准单元测试 275 项、失败 0、跳过 40，标准及 clone Debug 均构建成功。一次任务名 `testDebugUnitTest` 因有两个构建变体而失败，改为明确的 `testStandardDebugUnitTest` 后通过。
- 为验证真实电话框架，仅在临时源码副本构建一次性诊断 JAR，经临时 clone Debug 包运行：正式 Radio 注册并启动后，从其内部事件端点各投递一次合成短信及虚拟来电。该诊断没有宿主事件入口、不连接真实运营商；实际日志分别为 `RADIO_PROBE_SMS SUBMITTED`、`RADIO_PROBE_CALL SUBMITTED`。实机 Guest 显示 +10086 的来电，可接听并挂断；短信通知及收件箱显示 +10086 的测试正文 A。测试短信及通话记录保留在现有 Guest；不据此证明宿主桥接、通话音频或外发功能。
- 诊断包已由正式 clone 包覆盖；应用 APK SHA-256 `9623328a0a57f53a12086856cb86133d133b7e7bfbaf6a7f3823371787f11274`，运行副本及资产 Radio JAR SHA-256 `6282cab281d7f4a1a1bc33fd68ad1d0bb4de555d577932aa68cb14db2a7eb0ea`。正式版本重新启动后 boot completed、SIM LOADED、运营商 RedMagicEMU、网络类型 LTE，独立查询可见 config/default 和六个 slot1 服务；未进行长期稳定性测试。测试注入代码不在正式源码或正式 JAR 中。
- 宿主短信及来电按钮仍走旧 RIL，正式认证传输未接入，当前尚不能从宿主发起这些事件，单卡模块及 0.3.0 不通过。结果是 Guest 内模拟 Radio 到框架的试验；下一项应接入经过认证的宿主事件通道并从界面端到端验证。回滚可恢复本轮 SIM 模型与回归测试、重建服务 JAR 和 APK；原始固件未改。
- 两次试验关机调用仍返回 `Bad file descriptor`，关闭界面后 `:vm` 均已退出，关机流程未完整验收。操作期间曾在宿主其他应用处于前台时误触一次；发现后暂停并确认界面状态再继续。预检的设备诊断输出、截图和临时诊断 APK/JAR 已删除；未删除 Guest 中的测试短信或通话记录。

### 0.3.0 Guest 事件接收器（2026-11-07，模块未通过）

- 为已连接的本地 seqpacket 加入 Guest 逐帧接收器，使用 `SO_PASSCRED`、`recvmsg` 和既有凭据校验检查发送 PID/UID，拒绝截断、其他附加数据及错误来源，正常返回的 `SCM_RIGHTS` 描述符在拒绝前关闭；回复使用一次 `sendmsg` 并检查长度。调用方仍须认证连接、验证 socket 类型、在失败后实际结束整个服务进程；目前失败只标记需要退出，不能把它当成进程退出已执行。接收器与会话泵已加入服务 JAR，但未接到正式服务或宿主界面。
- 先行离线测试因接收器缺失失败；完整 Radio 回归输出 `RADIO_EVENT_SOCKET_TRANSPORT_OK`、`OFFLINE_RADIO_OK dex_classes=442`，Guest-only Binder 仅编译。服务构建白名单测试先因缺类失败，修改后 7 项中 6 项通过、1 项跳过；clone Debug 构建成功。
- 现有 clone 的独立 N64 Guest 合成进程实测：合法发送者消息被接受并经实际接收器回复，同 UID 子进程消息被拒绝，附带文件描述符的消息被拒绝；发送端在接收进程仍存活时观察到管道写端关闭。三组均报告通过，未使用真实短信或来电。首次诊断在成功收发后用普通 LocalSocket 流读取同步信号时返回 `Not a data message`；调整诊断同步方式后重跑三组通过。以上只证明 Guest 内独立进程的测试。
- 服务 JAR SHA-256 `045e9bfeef28a6863f34f8e426296787c9816886e374c452d211241f7615e52c`，clone APK SHA-256 `3d0b5cede9176c79407cce4016f2ce164df044e29c6714602f3dba4f520dc39f`。设备连接中断后重新连接，覆盖安装并启动现有 clone；运行副本 JAR 摘要与构建一致，Guest 报告 boot completed、SIM LOADED、RedMagicEMU 和 LTE，独立查询到 config/default 和六个 slot1 服务。未验证长期稳定性或宿主事件端到端。两次关机调用仍返回 `Bad file descriptor`，关闭界面后 `:vm` 均已退出；未认定关机流程通过。测试文件已清理。
- 仍须将经过认证的真实宿主连接、socket 类型检查、生命周期及退出监督接到正式端点，并通过宿主界面实测短信和来电；当前不开放未经认证的事件入口，单卡模块和 0.3.0 不通过。回滚可移除接收器和诊断、恢复服务构建白名单并重建应用，保留原始固件及其他工作。

### 0.3.0 宿主认证进程句柄（2026-11-07，模块未通过）

- 认证 launcher 原有 pidfd 由进程对象持有，并在 waitFor 后关闭；按旧 PID 再打开句柄存在复用竞态。增加严格启动选项：认证不可用时拒绝回退普通进程；同时允许在进程仍存活时，在与关闭互斥的短时回调内借用原 PID/pidfd。默认启动路径不变，正式 Radio 启动与事件连接尚未调用这两个新接口。借用方不得保存或关闭句柄、阻塞或等待进程，并须检查 pidfd 活性；这些是调用方契约，不由接口强制执行。
- 先行定向测试因缺少接口失败；完成后 6 项定向测试通过，覆盖严格模式不可用拒绝、默认回退、借用和退出关闭互斥及异常释放；clone Debug 构建成功。覆盖安装至现有 clone 后，Guest 报告 boot completed、SIM LOADED、RedMagicEMU 和 LTE。该启动走默认路径，不能当作严格 Radio 启动已验证。
- 直接以应用 UID 启动独立 app_process 诊断返回 `Aborted`，其简化检查报告 Dalvik 缓存所有权权限错误；APEX `dalvikvm64` 尝试也返回 `Aborted`。改用与 clone 同签名的临时 instrumentation 程序，普通 adb 安装返回 `INSTALL_FAILED_USER_RESTRICTED`；Root 安装获授权后，测试在 clone UID 和真实 APK 类/JNI 下输出 `STRICT_UNAVAILABLE_REJECTED`、`AUTHENTICATED_PIDFD_LIVE`、`AUTHENTICATED_EXIT_REJECTED` 和 `HOST_AUTH_DEVICE_PROBE_OK`。测试启动的是受控宿主子进程，未启动 Radio 或验证真实 socket。临时测试包及设备诊断文件已删除，未修改安全策略。
- clone APK SHA-256 `7f0f39e49263403e7700067e3e8afbaa872b55e7bc46b0f8ee146e3ed0356632` 与设备安装包一致；运行副本 Radio JAR 未变。Guest 关机调用仍返回 `Bad file descriptor`，关闭界面后 `:vm` 已退出。用户可见的短信/来电按钮仍走旧 RIL；真实认证连接、生命周期、会话握手及端到端投递均未完成，单卡模块与 0.3.0 不通过。下一项应将严格认证和原 pidfd 接到 Radio 专用连接，再验证双方身份；回滚可撤销上述接口、测试并重建应用。

### 0.3.0 宿主按钮到模拟基带（2026-11-07，最终修订待实机）

- 目标 AIDL 模拟基带启动时建立一次性的本地 seqpacket 连接；宿主用严格 launcher 的原 pidfd、对端 PID/UID 和逐包凭据认证，Guest 校验宿主 PID/UID，双方使用随机非零会话号。宿主短信/来电按钮在后台通过该连接提交到正式 Radio 端点；短信复用现有单条 PDU 编码，来电复用现有语音模型。旧 RIL 和关闭模拟基带路径按原选择分流；ACK 只表示本地提交，发送后超时或不确定时退役连接，不自动重发。该桥接不开放网络端口，也不访问真实 SIM 或宿主特权硬件节点。
- 手机上的初版 clone APK SHA-256 `0146911feb61f6140df274003cd24a00fd73ca0089155da3652f17c600776957`，日志显示 `radio: authenticated event channel ready`。实际点击宿主 Fake SMS 按钮后，Guest 通知及收件箱出现 `+10000000000`、`Hello from RedMagicEMU`；实际点击来电按钮后出现该号码来电，滑动接听进入计时界面，随后在 Guest 挂断。测试短信及通话记录保留在 Guest；两张测试效果截图仅保存在本机临时目录，不加入 Git。这是一台设备的一次端到端测试，不证明通话音频或真实运营商能力。
- 手机离开前的审查及后续电脑回归发现并修复：启动失败或停止时清理事件连接、失败清理不跳过其他资源、JNI 链接错误不掩盖不确定结果、停机与连接发布互斥，以及延迟按钮请求不得进入下一次 Guest 会话。DEBUG 导出广播的短信分支保留旧 RIL 路径，不调用新 AIDL 通道。生命周期回归验证门控原语，尚未直接实测最终 UI 的竞态路径；Guest 接收侧对宿主进程死亡、同 UID 保留旧连接且 PID 后续复用的极端情况尚无实证，不能称为完整安全证明。
- 最终本地完整 Radio 回归输出 `RADIO_EVENT_SERVICE_OK`、`OFFLINE_RADIO_OK dex_classes=444`；原生 6 项 gate、实际 Unix socket/pidfd、ACK 和描述符清理测试通过；服务构建边界 7 项中 6 项通过、1 项跳过。标准单元测试 290 项、失败 0、跳过 40；标准及 clone Debug 构建成功。最终 clone APK SHA-256 `c1319bdd7a683bba5f77a789ff4ead316cb7772d921b892291fc89303b3d3dfb`，服务 JAR `5c142ba399ceb0d324bca39b532a19b69e95c1bda07f333813e1ad0596626494`，宿主库 `c87781168db2b8435ead056ff2a9fe8698b90edbfe2d418f855d26be35a6f0a9`；已核对 APK 内对应资产摘要。最终 APK 尚未安装或实机运行。
- 设备暂停操作前，Guest 关机请求仍返回 `Bad file descriptor`；最后一次只读检查 `:vm` 仍在，不能称关机流程通过。单卡模块与 0.3.0 因最终修订未实机复测而不通过，不能将模拟 LTE 或有来电解释为完整原厂 Radio HAL。设备恢复后需在现有 clone 安装最终 APK，核对包及 JAR 摘要、认证连接 ready，再通过按钮各发一条短信和一次来电，检查收件箱及接听/挂断；保留旧实现、构建资产的回滚备份与原始固件。

### 0.3.0 虚拟短信与来电断连、重启本地回归（2026-11-07，待实机）

- 本机真实 Unix socket/pidfd 测试复现：同一已认证进程若重复发送原 hello，原协议因它和首条事件成功 ACK 的 epoch、序号和内容完全一致，会误把第二个 hello 视为首条事件成功确认。普通 seqpacket 不会自行复制消息，设备是否出现过此情形未验证。现将握手序号保留为 `Long.MAX_VALUE`，普通事件只用 1 到该值减 1；Guest 和宿主必须作为同一应用构建更新。Guest 先行测试因旧 hello 失败，宿主序号边界测试先失败，修改后通过；真实 socket 回归拒绝重放 hello，再接受对应的新事件 ACK。
- 修复 Radio 进程空闲退出后连接直到下一次发送才回收：独立监听线程等待所绑定的进程退出，并关闭该进程启动时捕获的连接，旧进程不会读取或关闭新会话连接。stop 与退出监听并发关闭时只执行一次，首次清理失败会尝试其余资源并持续报告原错误；stop 或启动失败路径只在成功关闭后丢弃引用。先行回归分别复现缺失退出回收、重用握手序号和关闭失败后误报成功，修复后通过；真实本机子进程也验证了退出后触发清理。正在等待 ACK 时关闭可能等待既有的 5 秒超时。
- Guest 本地测试使用真实共享模型与合成帧/回调：旧连接 EOF 后通话清理，新实例拒绝旧会话短信及来电帧，并接受新会话的首条短信与来电。native 真实 Unix socket/pidfd 测试覆盖连接断开、对端退出后的旧 pidfd、旧 epoch 以及重复握手包；均为本机测试。短信等待框架 ACK 时遇电源循环仍保持待确认，未以简单清空掩盖迟到 ACK 的关联风险。
- 完整离线 Radio 回归输出 `RADIO_EVENT_DISCONNECT_RESTART_OK`、`RADIO_EVENT_SERVICE_OK`、`OFFLINE_RADIO_OK dex_classes=446`，Guest-only Binder 测试仅编译。服务构建边界 7 项中 6 项通过、1 项跳过；扩充后的 native `RADIO_HOST_GATE_SOCKET_OK` 在 WSL/GCC 严格编译运行通过（另由测试代理交叉编译 Android ARM64 API 35，未在设备运行）。标准单元测试 298 项、失败 0、跳过 40；标准和 clone Debug 构建成功。
- 最终 clone APK SHA-256 `bc032710df28041948caac7145e6994ae3a185b3a8d4bcbd18e9fac53f679fd1`，内含 Radio JAR `0eb70e5c770fbeacbce06346a2a7a14f4fe446379073219c806f064f66a0f119`、宿主库 `c87781168db2b8435ead056ff2a9fe8698b90edbfe2d418f855d26be35a6f0a9`。设备操作结束后未再连接，新包未安装；上轮的按钮效果属于此前构建。此前关机仍返回 `Bad file descriptor`，最后检查 `:vm` 尚在，后续状态未知。
- 单卡模拟基带与 0.3.0 仍未通过最终实机验收。设备恢复后先确认并正常关闭原 Guest，在现有 clone 安装该构建，检查认证连接，再实测按钮短信/来电、断连后清理及重启后再次投递；如设备关机仍失败，先记录并处理，不强制标通过。回滚须同时恢复 Guest hello 与宿主握手预期、重新构建配套 JAR/APK；不要只回滚一端，原始固件不变。

### 0.3.0 短信待确认期间电源切换（2026-11-07，待实机）

- 本地在真实共享单卡模型和合成回调上复现：第一条短信已提交但框架确认未到，关闭再开启模拟无线电后，第二条在回调前被待确认状态拦截，却被事件端点标为 UNCERTAIN；宿主因此会退役整条连接。先行回归期望明确拒绝但实际得到状态 3；这是合成回调下的失败证据，设备上是否发生过尚未验证。
- 短信服务为内部端点提供明确的布尔结果：仅当上一条仍待确认、当前短信尚未调用回调时返回 false，端点据此返回 REJECTED；已调用回调后发生 `IllegalStateException` 或 `RemoteException` 仍为 UNCERTAIN。原公开注入方法的待确认异常语义保留；不在断电或恢复时清空旧待确认状态，也不自动重发被拒绝的短信。
- 新增本地回归覆盖第一条提交、关电期间拒绝、恢复后旧确认未到时拒绝且不重复回调、迟到确认后下一条提交，以及回调执行后抛相同异常仍为 UNCERTAIN。旧回归此前把待确认时的下一条错误预期为 UNCERTAIN，已改为明确拒绝；首次完整回归因此失败，更新该预期后通过。AIDL 确认没有短信关联标识，本轮不能保证区分重复或乱序的旧确认。
- 最终完整离线 Radio 回归输出 `RADIO_EVENT_SMS_ACK_RECOVERY_OK`、`OFFLINE_RADIO_OK dex_classes=447`；Guest-only Binder 测试仅编译。服务构建边界 7 项中 6 项通过、1 项跳过；标准及 clone Debug 构建成功，标准单元测试任务沿用 UP-TO-DATE 结果，本轮未重新执行。clone APK SHA-256 `11fefee4ec41791fe1d53018f9fb15bcf6da713741e9f04d96233db4443d73de`，内含 Radio JAR `964b5fa41f608ff054008ef36b1b8449916bc2e02f0d84e7dd9e883ad34046a7`，宿主库 `c87781168db2b8435ead056ff2a9fe8698b90edbfe2d418f855d26be35a6f0a9`；本轮设备离线，未安装或实测。
- 设备恢复后先确认并正常关闭原 Guest，再于现有 clone 安装此构建，检查按钮短信/来电、飞行模式切换和连续收信；若要准确控制迟到确认，还需可控设备诊断。上一条待确认期间的新短信不会排队，需在确认到达后重新发送。0.3.0 与单卡模块仍未通过最终实机验收；回滚可恢复服务端前置拒绝处理和对应测试，重建并部署配套 JAR/APK，原始固件不变。

### 0.3.0 虚拟短信超长提示（2026-11-07，待实机）

- 源码检查发现短信弹窗在提交时立即关闭，而编码器随后才检查单条 70 UTF-16 单元的上限；超长输入会收到笼统的失败提示并丢失输入。此为源码确认的行为，未在设备复现。
- 短信弹窗提交前复用编码器长度判断：超长时保留对话框和输入，显示上限及表情可能占两个单位的提示；修改至范围内可再次提交。不改变短信编码、基带协议或原始固件。回滚恢复弹窗、长度检查方法及对应文案和测试后重建 APK。
- 先添加边界测试，编译因缺少共享长度检查方法而失败；实现后 `VirtualSmsDeliverTest` 6 项通过，覆盖 70/71 个中文及表情占两个单元，与实际编码器结果一致。标准版及 clone Debug APK 离线构建成功。
- 新 clone APK SHA-256 `59b5087af2b0761ffed866e7dd2f7f8c2b3e1a15f82f74e558cfaa30f33d68e8`，设备离线，未安装或实测。设备恢复后先确认并正常关闭原 Guest，再安装到现有 clone，验证超长提示、保留并缩短输入后发送，以及虚拟短信和来电基础流程。单卡模块及 0.3.0 仍未通过最终实机验收。
- 修改后重新执行完整标准版本地单元测试（强制重跑），共 299 项、失败 0、错误 0、跳过 40；这是电脑端回归结果，不能代替界面或设备实测。

### 0.3.0 短信超长提示与按钮基础流程实测（2026-11-07）

- 无线连接手机后，安装前未发现 clone 或相关 Guest 进程；此前关机请求是否正常完成无法由此确认。保留现有 clone 数据覆盖安装，设备上的 APK SHA-256 为 `59b5087af2b0761ffed866e7dd2f7f8c2b3e1a15f82f74e558cfaa30f33d68e8`，与本地构建一致。启动时系统显示 16 KB 页面兼容性提示，点“确定”后继续；Guest 在 43 秒报告启动完成，日志出现 `radio: authenticated event channel ready`，上滑后观察到桌面。
- 在宿主 Fake SMS 弹窗输入 71 个 ASCII 字符并点发送：弹窗保持、原文仍在，显示 70 单元限制提示；删去一个字符后提示消失，点击发送关闭弹窗。Guest 短信应用收到同一号码及修改后的测试内容，实际验证了该次宿主按钮经认证桥接和模拟基带的投递。
- 通过宿主来电按钮触发虚拟来电，Guest 显示测试号码；接听后出现计时界面，Guest 挂断后退出通话。该结果只验证本次模拟来电状态及界面，不证明通话音频或真实运营商功能。
- 本轮未实测 Guest 重新启动、断连清理、飞行模式切换后的短信和来电，也未验证旧关机错误是否消失；单卡模块与 0.3.0 仍未完成最终验收。测试短信和通话记录留在现有 Guest；测试结束 Guest 仍运行，未强制停止。

### 0.3.0 Guest 关机后重新启动及短信、来电（2026-11-07）

- 现有 clone、APK SHA-256 `59b5087af2b0761ffed866e7dd2f7f8c2b3e1a15f82f74e558cfaa30f33d68e8`：从宿主 Power menu 打开 Guest 原厂电源菜单，两次点击关机。日志记录 `guest requested power off (userrequested)`、`stopping system` 和清理 136 个进程，界面显示 `Guest powered off`；检查仅剩应用主进程及 `:vm` 宿主服务，未以 `:vm` 退出作为 Guest 关闭条件。本轮未逐项验证 Guest 所有关机广播或写入持久化完成。
- 点击界面的重试按钮重新启动同一实例；日志记录认证事件连接 ready，41 秒后 Guest 报告启动完成，之后观察到桌面。通过宿主 Fake SMS 发送带 `restart-2026` 标记的消息，Guest 通知显示完整标记和测试号码；随后宿主来电按钮触发来电，Guest 显示号码，接听后计时、挂断后返回桌面。测试短信和通话记录保留在 Guest，测试结束 Guest 仍运行。
- 本次原厂菜单关机、重启和按钮基础流程未发现失败，未修改代码或重新构建。此次不是测试宿主菜单“关闭系统”路径；断连、飞行模式切换及长期稳定性仍未在当前构建设备上验证。单卡基带为宿主桥接模拟；0.3.0 尚未最终验收。

### 0.3.0 二次来电与提示调整（2026-11-07，待实机）

- 回归记录显示首次来电挂断后第二次来电提示失败；在设备当前旧 clone 构建上通过按钮再次触发时复现相同失败提示，未能从该提示区分明确拒绝、通道不可用或结果不确定。Guest `logcat` 报告 logd 未运行，未确认框架实际使用的挂断接口，不能断言已定位手机故障根因。
- 本地发现单卡服务此前不支持 `hangupForegroundResumeBackground`：新增回归在旧代码返回 REQUEST_NOT_SUPPORTED 时失败；现对唯一 ACTIVE 通话执行清理、响应与状态通知，无活动通话返回 INVALID_STATE。回归覆盖接听、前台挂断、状态为空和后续来电，完整离线 Radio 回归通过（`RADIO_VOICE_CONTRACT_OK`、`OFFLINE_RADIO_OK dex_classes=447`）；Guest Binder 测试仅编译。此补丁能否解决手机上的二次来电故障仍待安装实测。
- 不再在 Guest 报告启动完成后持续显示“桌面尚未确认”悬浮提示，启动过程提示保留；短信及虚拟来电成功提交时的文案改为“已完成操作”，失败提示保留。启动提示先行回归失败后通过；短信编码与启动提示定向单元测试共 7 项通过，标准和 clone Debug 构建成功。新 clone APK SHA-256 `e63fce4a12e662a31b02ad50bf2e0d9c5b993572f6ad918fb506345f16b231d3`，APK 内 Radio JAR SHA-256 `34ed26ac308c9a85006605415767356cc62650951531babb7e46703956703445`；设备操作结束后未安装、运行或继续操作。
- 宿主“关闭系统”的旧 `Bad file descriptor` 暂不处理；停止前在旧构建上操作一次该菜单，返回主界面且 Guest 进程退出，未重现异常，旧错误原因仍未知，未作修复。下一次设备可用时覆盖安装新包，在现有 clone 中连续两次来电、接听及挂断，同时确认启动和成功提示的实际显示；模块与 0.3.0 仍未最终验收。回滚可恢复此前服务资源 `app/build/rme-radio-before-foreground-20261107.jar`、本次源码和文案后重建；原始固件不变。

### 0.3.0 连续来电与提示实测（2026-11-07）

- 在已停止的现有 clone 覆盖安装 APK `e63fce4a12e662a31b02ad50bf2e0d9c5b993572f6ad918fb506345f16b231d3`，设备安装包摘要一致；运行副本 Radio JAR 摘要为 `34ed26ac308c9a85006605415767356cc62650951531babb7e46703956703445`。Guest 在 46 秒报告启动完成，认证事件连接 ready；桌面上未再显示“系统报告启动完成；桌面尚未确认”。
- 宿主按钮触发第一通来电，Guest 显示测试号码、接听后计时并挂断；随后再次触发，第二通同样出现、接听并挂断，未出现此前提交失败提示。两次来电的成功提示显示“已完成操作”。本次实测证明该构建在一次连续接听及挂断流程中可用；因没有捕获旧版框架调用，不能断言此前失败的确切根因已得到证明。
- 宿主按钮发送标记短信，成功提示为“已完成操作”，Guest 通知显示对应号码及完整测试内容。测试短信和通话记录保留在 Guest，测试结束 Guest 仍运行。
- 单卡模拟基带通过宿主认证桥接提供虚拟短信与来电。断连、飞行模式恢复和长期稳定性尚未实测，单卡模块及 0.3.0 未最终验收；旧 `Bad file descriptor` 继续搁置。
- 本轮验收记录覆盖连续两次来电接听与挂断、启动提示及短信和来电成功提示；范围以本节一次实机测试为准，单卡模块和 0.3.0 的其他待检查项状态不变。

### 0.3.0 未接听挂断后第二次来电故障（2026-11-07，未修复）

- 回归记录显示 Guest 飞行模式往返和短信正常；第一次虚拟来电未接听直接挂断后，第二次来电提示“提交失败，部分事件可能已发送”，随后飞行模式无法关闭。提示合并多种失败结果，不能仅凭文案判断是否送达或具体原因；没有自动重发。此前两次接听后挂断通过的范围不涵盖本场景。
- 只读核对设备 clone APK SHA-256 为 `e63fce4a12e662a31b02ad50bf2e0d9c5b993572f6ad918fb506345f16b231d3`，运行副本 Radio JAR 为 `34ed26ac308c9a85006605415767356cc62650951531babb7e46703956703445`，与此前记录一致。现场 Telecom 有一条来电处于 DISCONNECTING，同时仍列于响铃列表，并记录 REQUEST_REJECT、REQUEST_DISCONNECT；电话状态同样仍记录响铃。Radio 进程存活，语音工作线程两次采样均等待 futex；无法由此确定锁、实际 Radio 方法或死锁，也未证明飞行模式故障与来电故障同因。
- 宿主自动触控被权限拒绝，未修改权限或宿主安全策略。只读诊断临时文件已清理，未重启 Guest，未更改产品代码、构建或安装。已有合成测试覆盖 rejectCall 后再次来电，不能代替实际框架路径。下一步须取得实际框架请求和回调的受限证据，再建立对应失败测试、修复并实机复测；单卡模块仍未通过。

### 0.3.0 未接听挂断及飞行模式恢复实测（2026-11-07）

- 旧故障现场只读核查：Radio 语音及清理线程等待不同匿名地址，不足以判定死锁；不能将等待状态当作根因。用 JDK 21 重建未插桩 Radio JAR 与设备原 JAR `34ed26ac308c9a85006605415767356cc62650951531babb7e46703956703445` 摘要完全一致，排除当前源码基线差异。临时诊断包仅在固定方法及回调进出记录有界事件，不记录号码或短信内容；插桩可能影响时序。
- 临时诊断版实机复现首次来电直接拒接、第二次无来电：首次注入回调完成，拒接时调用 `hangupWaitingOrBackground`，原实现返回 REQUEST_NOT_SUPPORTED；第二次注入没有响铃回调，电话框架仍有 DISCONNECTING 和 ringing 来电。由实际调用和源码可定位该挂断接口未实现导致呼叫未清理；诊断事件不直接记录返回码，不能仅凭日志声称框架已收到回调。飞行模式此前卡住的具体因果未单独确定。
- 单通话模型现将该接口排入现有语音队列：仅响铃来电清理并发送状态通知，无来电或已接通时返回 INVALID_STATE。先行回归在旧实现对响铃返回 REQUEST_NOT_SUPPORTED 时失败；修复后覆盖响铃清理、再次来电、空状态和已接通保护。JDK 21 完整离线 Radio 回归通过 `RADIO_VOICE_CONTRACT_OK`、`OFFLINE_RADIO_OK dex_classes=447`，Guest-only Binder 测试仅编译。标准 Debug 单测 299 项、失败 0、跳过 40；标准及 clone Debug 构建成功。默认 Gradle Java 路径失效，本轮命令临时指定 JDK 21，未修改配置。
- 覆盖安装 clone APK `1744d874c80d66c05fb2d3df312d769139265dba90336b9ad3cb2c48de6ab611`，设备安装包摘要一致，运行副本 Radio JAR 摘要 `97b66aabeb33a96550b4e59857f3fe381f46d66f11d369f0137a60a531a17636` 一致；Guest 41 秒报告启动完成且认证连接 ready。第一次来电未接听直接拒接，第二次来电正常显示并拒接；Guest 快捷设置打开再关闭飞行模式，4G 和 RedMagicEMU 恢复。电话框架查询当前无响铃、前台或后台通话，飞行模式设置为关闭。恢复后宿主按钮发送带唯一标记的短信，Guest 收件箱可查到；再发起虚拟来电，Guest 接听后计时并挂断。该次测试短信和通话记录保留在 Guest，结束时 Guest 保持运行。
- 这是一次设备验证，飞行模式旧故障是否与未清理通话同因尚未证明。本次单卡复测以飞行模式往返后的短信和来电为范围。虚拟短信和来电经宿主认证桥接。回滚可恢复 `RadioVoiceService.java` 中本轮接口及回归测试，使用本机临时备份 `rme-radio-before-waiting-20261107.jar` 恢复原服务资产并重建 APK；不要重置其他未提交工作或修改原始固件。

### 0.3.0 单卡模拟基带基本功能验收确认（2026-11-07）

- 模拟基带小节按既有设备结果记为基本功能验收通过，覆盖宿主认证桥接的虚拟短信、虚拟来电接听与拒接，以及一次飞行模式往返后的短信和来电。断连和长期稳定性仍未实测。

### 0.3.0 Guest 以太网基本连通性检查（2026-11-07）

- 当前 clone 实例中，Guest 的默认网络为已连接且框架标记 VALIDATED 的以太网；通过应用现有私有 Guest shell 对 `https://connectivitycheck.gstatic.com/generate_204` 执行一次限时 HTTPS GET，证书验证成功且返回 HTTP 204。临时查询输出已清理；未开启网络 Guest ADB，也未修改网络或安全策略。
- 这证明本次设备的基本 HTTPS 路径可用。未改网络实现；按优先顺序继续检查音频基本播放，按键音效仍搁置。

### 0.3.0 基本音频播放检查（2026-11-07）

- 本轮选择基本音频播放，预期 Guest 产生受控 48 kHz、立体声 PCM 并经宿主扬声器播放；暂不处理按键音效。初版 2 秒探针写入 192000 个样本，但实机未听到声音；该探针振幅较低且写入后只等待 300 ms 才释放。本次记录探针与 Guest 媒体音量。
- 改进的一次性 Guest 探针无权限请求，输出两秒 1 kHz 合成音并等待播放头到达目标；界面显示写入 192000 个样本、播放头 96000/96000 帧，实机听感确认有声音。同一宿主 `aemu-audio` 线程在播放前后的读取计数增加 1933312 字节，时序与播放一致；Guest 音频服务映射的模拟 primary HAL 与引擎资产摘要一致，运行副本的 PCM FIFO 及宿主音频线程存在。数据计数只能支持桥接活动，不能单独证明音频内容。
- 本次在一台设备上证明该合成音可经模拟 HAL 和宿主桥播放；未修改产品代码、构建或安装产品 APK。未验证普通应用媒体格式、录音、按键音效、长期稳定性或原厂 DSP/HAL 完整兼容；音频播放与录音模块不标为整体通过。下一独立任务应在具备可验证输入和权限的条件下检查录音。

### 0.3.0 原厂铃声索引与实际来电排查（2026-11-07，未完成）

- 当前 Guest 来电、通知、闹钟原本均显示“无”，三个 settings 值与对应 `_set` 标识均不存在；默认 OGG 文件存在于 `/product/media/audio`。内部媒体数据库只读副本的 files 表最初为 0。不能仅凭空值断言实例曾设置静音。
- 单文件扫描使默认来电 OGG 入库；官方 `scan_volume internal` 返回空 Bundle，后续索引未增加，不能把调用返回视作扫描成功。对固定系统目录 `/product/media/audio` 调用 MediaProvider `scan_file` 后，内部库有 136 条音频，按分类标志来电 29、通知 36、闹钟 31，三个默认 OGG 均在库；扫描未触碰宿主共享目录。原厂选择器试听 `1Alpha_5.ogg` 有声音。通过 Guest 设置界面将来电、通知、闹钟设置为各自原厂默认，分别解析到 `1Alpha_5.ogg`、`1Bounce.ogg`、`1Quantum_Foam.ogg`。Guest 原厂菜单正常关机重启后，三个 URI 和媒体索引均保留。本次只修复当前运行副本索引及该实例设置；全新导入时自动扫描/默认初始化缺失的原因未证实，尚未修改产品代码。
- 重启后虚拟来电界面显示来电，但实机未听到铃声；Guest STREAM_RING 未静音、音量 10/15、speaker，铃声模式 NORMAL，勿扰关闭，宿主媒体 speaker 未静音。Telecom 记录 START_RINGER，约两分钟超时后 STOP_RINGER；这不证明播放器真正开始输出。再次来电期间 Guest ringtone-player 线程被观察到，采样时在等待；同一宿主音频桥线程连续四秒读取计数无增长。一次 Guest AudioFlinger 状态查询未及时返回，不能仅据此判断死锁或根因。来电音仍未修复；通知、闹钟实际触发声音尚未实测，不证明原厂 DSP/HAL 完整兼容。
- 未修改产品源码、未重新构建或覆盖安装；测试通话记录保留。回滚当前 Guest 铃声设置可在各自设置页面选择“无”；媒体库索引为扫描所得，可由系统重扫重建，不清空数据库。后续应在同一故障窗口核对铃声播放器与音频输出等待链，先建立失败验证再最小修复；仅靠 PCM 合成音或铃声试听不能验收来电响铃。

- 进一步对照：实际来电失败后，在设置界面再次试听同一默认铃声，实机听感正常；无权限自造 Guest 探针使用 RINGTONE/SONIFICATION 音频属性分别非循环和循环播放同一默认铃声，两种模式均可听，且探针报告播放状态为 true。虚拟来电响铃期间播放该探针铃声，探针声音仍可听，但来电自身不响；这支持普通播放和来电状态下的扬声器路径可用。一次 Guest AudioService 快照中焦点栈为空、系统 UID 铃声用途 MediaPlayer 为 idle；该快照未同时独立核实来电仍响铃。当前实际来电无声仍未修复；没有为了掩盖故障替换系统组件或关闭检查。

### 0.3.0 来电铃声兼容及系统音频索引（2026-11-08，部分完成）

- 目标 Guest 的 Telecom 实际代码和本轮日志确认：原厂铃声同频振动模式会跳过普通 Ringtone，转交未建立桥接的 RichTap 路径。`vibration_effect=0` 时未听到来电；临时改成 1 后来电有声，实验后恢复原设置。
- RedMagicEMU 对 NX809J/API36/ARM64/N64 的 Guest 启动属性声明 `ro.vendor.feature.zte_feature_richtap_mode=false`，不写宿主属性或硬件节点、不改实例振动设置；这会影响 Guest 中读取该 RichTap 功能声明的其他应用，同频振动能力不可用。保留原始固件；调试 `props.extra` 仍可显式覆盖。先行测试失败后，目标及非目标属性定向回归通过。
- 自动索引在目标 Guest 报告启动后，后台调用 MediaProvider 仅扫描 `/product/media/audio`，检查三个原厂默认音频的分类记录；不设置铃声、不扫描共享目录。最多尝试三次并记录阶段结果，停止期间不继续重试；命令超时不证明 MediaProvider 内部工作已取消。目录扫描会协调其内部媒体索引；保留原选择仍须更广泛验证。
- 标准单元测试 315 项、失败 0、跳过 40；标准及 clone Debug 构建成功。最终 clone APK `8feae3230064362083478065f6e995f237b1d9c9e7c63bb075eea124087c44e3` 覆盖安装到现有实例，设备摘要一致。Guest 40 秒报告启动，索引第一次未返回有效 STREAM、第二次成功；三项既有铃声 URI 和振动模式保留，目标 RichTap 属性实际为 false。临时将 Guest 振动模式置回此前故障条件 0，虚拟来电响铃；随后恢复保存值 1。结果是宿主桥接播放。
- 未完成：新导入实例尚未独立实测，自动扫描仅使铃声入库，不会自动设置默认铃声；系统已有默认初始化函数在目标固件中可能覆盖已有选值并执行双卡附加操作，因此不能直接对旧实例调用。安全的首次初始化还需可信新实例标记、输入门控、设置竞态和异常中断处理，以及独立设备验证。不能把新实例自动默认铃声记为完成。回滚可移除目标属性覆盖及后台索引接入、对应策略和测试后重建；原有 Guest 铃声设置保持不变。

### 0.3.0 铃声扫描就绪探测及默认初始化调查（2026-11-08，待实机）

- 已在 `8feae3230064362083478065f6e995f237b1d9c9e7c63bb075eea124087c44e3` 版现有 Guest 实测：原故障模式 0 下实际虚拟来电响铃，之后模式恢复为保存值 1；原有三项铃声 URI 保留。该版第一次扫描无有效 STREAM，5 秒后第二次扫描成功。不能由此证明原厂 RichTap、振动或 DSP 兼容。
- 后续收紧测试版 `599f0b7f4c7ab2c4764709e478771ad42e4262b5c6be6026cf67b8bb0909c96f` 在现有 Guest 首次扫描未返回 STREAM 且未重试；分类诊断版 `79eb8809139d85b9828ec982b79eb8b9f005befcf1adc9b545c17d61bea9898f` 同一阶段报告 exception text，无重试。两版设置均未被覆盖；仅凭分类不能确定异常根因。
- 电脑端进一步改为先最多三次只读查询等待媒体服务响应，再最多一次扫描固定系统音频目录并验证三项；查询输出或扫描输出混有异常时不报成功，超时或扫描失败不重复发起扫描。新增失败先行及恢复、取消、防误报回归；最后完整标准单测 324 项、失败 0、跳过 40，标准和 clone Debug 构建成功。最新 clone APK `527abdb09b3b0c93c80cace549487f7ca4db004ba118b39f5ce79fec7237f803` 本地构建，设备操作结束后未安装或实测；不得将先前版本结果当作此版验证。
- 新实例自动默认铃声仍未实现：只读 Guest 辅助探针创建 system Context、检查 WRITE_SETTINGS 与 AppOps 显示允许，但通过 Settings/RingtoneManager 读取三项设置均抛 SecurityException；目标原厂框架的默认初始化还可能改双卡设置，且既有配置选择后 `_set` 仍可能为空，不能以此判定是否允许覆盖。需要可信新实例标记、输入门控及可验证的框架入口，再用独立新实例实测；当前仅自动索引。
- 手机最后由代理确认时 Guest 仍运行、宿主浮动菜单打开，未触发关机；之后设备暂停操作，实际状态未知。未修改原始固件或宿主安全策略；本轮设备临时探针和传输文件已清理。扫描超时后，MediaProvider 内任务仍可能运行。
- 只读分析产生的本机固件框架临时副本仍位于系统临时目录 `rme-framework-read-vbgid9_2`；先前清理尝试未成功，具体拒绝原因未能核实，未加入 Git。

### 0.3.0 新实例默认铃声离线核查（2026-11-08，未实现）

- 本轮仅在电脑继续核查，未操作设备，也未新增运行时代码或未消费的初始化标记。目标固件 RingtoneManager 的设置路径除写设置外还更新铃声缓存，闹钟类型另发厂商广播；不能将简单 `settings put` 当作已验证的等价实现。本机缺少目标 SettingsProvider 文件，无法离线确认它是否自行补齐缓存；此前只读 `app_process` 探针访问设置服务遇到 `SecurityException`，不能推断写入可行或绕过访问检查。
- 首次启动前可信空数据可用于识别新实例，但 Guest 在 BOOTING 时已有触摸和按键入口，另有异步输入队列、调试命令、终端、ADB 及 Guest 自身后台操作；bootCount、UI 遮罩、空 URI 和 `_set` 标志均用于辅助判断，已有铃声选择需要单独保留。执行器超时也不能仅凭发起进程终止断定 provider 不会迟到写入；首次启动门控、身份权限、退出确认、部分写入及重启恢复必须共同验证。原厂 ensureDefaultRingtones 还包含双卡附加行为，不能直接对现有实例调用。
- 相关 TargetRingtoneIndexTest 与 TargetRichTapPropertyTest 定向单元测试重跑通过；这些仅覆盖现有索引与能力声明，不证明新实例默认值已设置。待设备和隔离新实例可用时，先以已注册的 Guest 应用身份只读核对 Context、WRITE_SETTINGS、AppOps 与 canWrite，再在隔离的新实例中验证框架写入一项后的 URI、缓存、厂商广播和重启行为，同时核验现有实例自选/静音未被覆盖。新实例默认铃声自动初始化仍未实现。

### 0.3.0 新实例铃声初始化电脑端准备（2026-11-08，未接入）

- 仅在电脑完成首次初始化状态和会话输入门控的独立原型；没有接入 Guest 启动或输入服务，也未设置铃声。状态模型覆盖异常中断、持久化确认、单对象并发写入竞争等情况；输入门控覆盖旧会话输入失效、生命周期重入及并发串行化。定向测试分别为 12 项和 7 项，均通过。
- 独立审查确认：状态模型只相信调用方报告的持久化与写入停止，单对象的锁不能保证跨对象或跨进程唯一写入；门控与状态终态尚未原子协调，也不覆盖其他输入路径或回调之后排队的工作，持锁回调还必须避免等待另一个需要同一锁的线程。
- 已保存独立的普通 Guest 应用只读探针源码和构建检查方法，仅读取写设置权限状态及三类默认铃声 URI 是否存在和是否为音频 MIME，不显示 URI，也不申请或修改权限、设置。此前临时探针的启动类配置与 DEX 不一致；新包从持久源码构建，静态检查通过，反向回归确认旧错误会被拒绝。目录不含 APK 或密钥，不自动安装或接入产品；实际 Guest 能否启动及权限结果待设备实测。
- 标准版单元测试 343 项、失败 0、错误 0、跳过 40；标准版及 clone Debug 离线构建成功。新构建 clone APK SHA-256 为 `48f34a2e4d51c5dff79df2e764f6d919bcfe3828c1ce43f4c698b283796b39ef`；本轮未操作手机、未安装或实测该包。此前经宿主音频桥听到来电铃声的结果不证明原厂音频 HAL、RichTap 或 DSP 完整兼容。

### 0.3.0 铃声扫描新版与权限实测（2026-11-08）

- 手机原有 clone 实例经界面重试后启动，Guest 界面可用，引擎与 Binder 服务运行。先前尝试以独立引擎进程读取 `sys.boot_completed=1`，经型号与 SDK 对照发现该调用实际读取宿主属性，该结果不能作为 Guest 启动证据。独立只读诊断应用通过 RedMagicEMU 安装入口在原有 Guest 启动：目标 API 36、Guest 用户 0，`WRITE_SETTINGS` 检查和 `Settings.System.canWrite` 均为 false；三类默认铃声 URI 均存在且 MIME 为音频。此结果证明普通应用身份可读取当前值，但不证明有权限写入或新实例默认值自动初始化。自造探针已通过 Guest 包管理器卸载，本次推送的 APK 已清理。
- 正常关闭 Guest 后，覆盖安装 clone APK `48f34a2e4d51c5dff79df2e764f6d919bcfe3828c1ce43f4c698b283796b39ef`，设备安装摘要一致。新版启动后媒体服务首次只读就绪查询未通过，第二次后扫描返回 STREAM，三类分类检查通过并报告 `system audio indexed`。只读查询显示当前来电、通知、闹钟仍分别指向既有配置 `1Alpha_5.ogg`、`1Bounce.ogg`、`1Quantum_Foam.ogg`；本版一次虚拟来电已通过听感确认。该次测试会在 Guest 留下通话记录。
- 目标原厂 MediaProvider 反汇编表明：特定非 internal 卷扫描路径会在服务上下文调用 `RingtoneManager.ensureDefaultRingtones`；内部卷或目录的 `scan_file` 并不直接走此路径。该原厂初始化在 `_set` 标志缺失时可能覆盖既有选择，也有双卡附加操作；不能对现有实例直接触发。历史 MediaProvider 日志有挂载事件因卷已附加而跳过，。新实例首次初始化缺失的原因未确定。保留原始固件；本次分析临时副本已清理。
- 现有实例实测响铃是通过模拟音频与宿主桥播放。新实例自动默认铃声尚未实现，通知与闹钟的实际触发声音、录音及长期稳定性待设备检查。
- 后续选择删除旧 clone 的 Guest、改用 clone 做全新实例测试。旧 Guest 已正常关闭并随 clone 卸载而删除；重新安装同一 `48f34a2e…` 包后开始从原始 `RedMagic_NX809J_VM.zip` 导入，原固件摘要与既有记录一致。曾创建的独立测试包在导入中取消，暂存已清理、测试包已卸载；这不改变上述针对旧实例的历史实测事实。
- 全新 clone 实例发布后首次启动前，其数据目录约 4 KiB，所检查的系统设置及包管理文件不存在；元数据目标匹配且启动计数为 0。首次启动日志在 125 秒后报告完成，铃声索引第二次就绪查询后返回 STREAM 并报告成功；随后通过 RedMagicEMU 的 Guest 终端读取 `ringtone`、`notification_sound`、`alarm_alert`，三项均为 `null`。当时 Guest 实际仍在原厂欢迎引导，不能用这次空值推断引导完成后的状态。首次启动后按系统界面授予 clone 通知权限，未授予全部文件访问权限。
- 完成原厂引导：同意引导所需协议，跳过账号、迁移和声音录入，关闭 Guest 固件自动下载及夜间安装，进入桌面后 `device_provisioned` 与 `user_setup_complete` 均为 1；再次只读查询三项铃声仍为 `null`。本次测试还在引导中关闭了可选天气和锁屏画报，未开启魔姬助手权限；这些是此实例的设置，不是模拟器全局策略。
- 仅对该新实例进行一次诊断：先确认其 emulated 卷已挂载、`external_primary` 音频查询可访问，且实例共享路径位于 clone 的应用专用外部目录；通过 Guest 终端执行一次原厂 MediaProvider `scan_volume external_primary`。调用返回空 Bundle 本身不能说明成功，但执行后实测三项设置均由 null 变为 URI，分别解析到 `1Alpha_5.ogg`、`1Bounce.ogg`、`1Quantum_Foam.ogg`，新实例虚拟来电响铃。该扫描还会扫描此实例的外部目录；它是对当前实例的一次手动诊断，不是 RedMagicEMU 中的自动默认设置实现。无法仅凭这一次对照断言此前默认设置缺失的全部因果。
- 三项对应的系统 `_set` 标记均为 1，缓存文件均非空。通过 Guest 正常重启后，日志在 39 秒报告启动完成、系统音频索引成功；三项 URI、标记与缓存大小均保持一致，虚拟来电响铃已实机确认。测试通话记录留在新 Guest。只证明此设备此实例的一次重启与来电。
- 原厂外部卷扫描可能在标记缺失时覆盖已有配置，并触及该实例的整个外部共享目录；不能无条件在每次启动时调用。现有新实例状态、输入门控和运行租约仍是未接入的原型，未证明实例输入、Guest 后台写入、超时迟到写入与中断恢复安全；新实例自动默认铃声仍未实现。保留现有设置与隔离边界优先，不能将本次人工设备诊断误记为产品自动修复。
- 手机临时测试 APK 和独立测试包已删除；电脑上为构建该独立包创建的临时目录 `%TEMP%\rme-isolated-build-ef5c27b27472463c9a31991e2f3b6807` 仍在。

### 0.3.0 新实例自动铃声的安全准备（2026-11-08，未接入）

- 针对发布后新实例需要自动准备原厂铃声，设备上的只读诊断确认：目标 SettingsProvider 对临时测试键的“键不存在”和“键存在且为 null”有不同查询结果；删除测试键后查询和持久文件均不再包含该键。不能由此直接推断原厂铃声的静音写入语义，也不能忽略并发选择问题；未修改现有铃声或宿主权限。
- 在同一台手机以 clone 普通应用 UID 运行四项自造进程合成测试：进程监管测试正确回收自有脱离子进程且未影响同 UID 旁观者；失败与崩溃测试保留不确定标记；模拟启动协议通过；父死信号测试确认不自动保护子孙进程。测试文件已清理，没有操作真实 Guest 进程。结果只覆盖本机合成子树，不能当作真实 Guest 生命周期或其他设备已验证。
- 按原厂界面暂时将当前 Guest 的通知铃声设为“无”：系统设置键仍存在、值为实际 null，`notification_sound_set` 为 1。随后通过原厂界面恢复“弹力按钮”，媒体 ID 与缓存摘要和测试前一致；设置 URI 从带 canonical 参数变为原厂界面写入的同 ID 普通 URI，故不是字节级恢复。未修改来电、闹钟或宿主权限。不能仅以一次通知铃声测试推断所有铃声及并发情况下的行为。
- 在正常关闭 Guest 后，临时覆盖同包名 clone debug 包，在宿主应用自身 ART 和普通 UID 内对私有临时目录实测安全目录流、文件/目录同步、同进程双线程单次认领、重开及静态符号链接拒绝，均报告通过并清理临时目录。随后恢复安装原 `48f34a2e…` 包并重新启动 Guest，日志报告 40 秒启动和系统音频索引成功。该设备测试不证明掉电后的持久化、跨进程目录替换或真实 Guest 写入者已停止；身份复核代码调整发生在该次设备测试后，尚待复测。
- 状态组件增加目录身份复核后，再次用同包名临时 debug 探针实测：打开、同步、双线程单次认领、完成后重开、符号链接拒绝，以及打开句柄后替换目录时拒绝认领和完成均返回 true，临时文件已清理。随后恢复原 `48f34a2e…` clone，Guest 日志报告 39 秒启动、系统音频索引成功。该测试不覆盖最后一次身份检查后的替换竞态、掉电持久化或真实 Guest 进程退出；组件当前保留为独立实验。
- 新增未接入启动的首次铃声状态、受控扫描策略和有界命令执行基础件，以及并发、身份变更、超时/输出不完整等回归。独立审查发现输入门控实验会改变已工作实例的输入路径，且旧 Guest 的退出未能确认，因此已保存本机实验补丁并撤回产品输入改动。标准单元测试 384 项、失败 0、错误 0、跳过 42；标准版和 clone Debug 构建成功。临时诊断 receiver 已从最终构建中移除，最终构建 clone APK `9ce9def5e0e8e107b5878a9354181c091278466f6d3e5715caf79a70fd674cf9` 未安装；手机仍运行此前设备验证版。新实例自动准备仍未完成，不应按可发布功能验收。
- 新实例方向确定为自动准备，并接受首次启动额外等待；目标是在独立空卷中运行原厂初始化，确认写入与 Guest 写入者退出后才发布实例并使用正常共享目录。该路径仍未接入，不能将方案或基础件记为自动功能通过。
- 新增未接入 App 的私有临时卷 C 基础件与合成测试：通过受信任目录描述符独占创建、不复用已有目录，清理前要求调用者确认租约和 Guest 写入者退出。WSL 测试、ASan/UBSan 通过；手机以 clone 普通应用 UID 运行 ARM64 合成测试通过，硬链接子项因设备 `EACCES` 明确跳过。测试仅限自建目录；本次临时设备文件已清理。
- 新增未接入 App 的初始化进程退出观察基础件：只使用 `waitid(...WNOWAIT)` 判断子进程是否为空，不杀进程、不收割 JVM 的退出状态；存活或未回收的子进程使检查保持未完成。WSL 自造进程及故障注入测试通过；手机 clone 普通 UID 的 ARM64 两项合成程序均通过，测试二进制已清理。仅证明本机合成场景可用，尚未与实际 Guest、宿主写入线程或导入恢复接线，不能作为发布或清理实例的证明。
- 新增待接入的导入隔离标记：在启动任何 Guest 写入者前持久化标记；标记存在、损坏或无法确认时保留待导入目录和隐藏 reservation，普通发布会拒绝。Windows 定向回归 22 项、失败 0、跳过 3；新增的 8 项在 WSL Linux 临时 JRE 与本地文件系统中全部运行通过，包括目录同步、故障注入和子进程退出。该入口尚无生产调用；租约文件描述符身份、真实 Guest 子代和安全恢复仍未证明，也没有提供清除隔离或发布接口。
- 临时诊断在同一设备、同一 Guest 正常运行并关机后观察到，Guest 状态为 STOPPED，但隔离退出检查超时；同一宿主进程仍收养了多项已退出的 Guest 僵尸子进程。测试只观察进程，未清空 Guest 数据。诊断包已替换回先前实测的 clone APK `48f34a2e4d51c5dff79df2e764f6d919bcfe3828c1ce43f4c698b283796b39ef`，当前 Guest 恢复启动并报告 `guest reported boot completed` 和 `system audio indexed`；这些不证明新实例铃声自动设置完成。回归结果暂停继续扩大隔离与进程管理方案，重新评估更简单的铃声初始化入口。
- 暂停该方案后，移除临时设备探针源码并重跑标准版单元测试和标准版、clone Debug 构建：399 项测试，失败 0、错误 0、跳过 49；构建成功。新构建 clone APK SHA-256 为 `6b06ca2ed7785f49ec8da93b3e850fcbe68d3dbb2e1ff6f1018c4ee8e3df136d`，未安装、未实机验证。隔离状态原型尚有通知重复和父目录同步等已知限制，不作为发布功能。
- 进一步只读反汇编当前 Guest 运行副本的目标 MediaProvider：该 APK 静态引用中 `ensureDefaultRingtones` 仅见于 `MediaService.onScanVolume`；`scan_volume` 以卷名经 VolumeCache 查找有效卷，非 internal 且无路径时会在初始化前返回。有效外部卷会先扫描内部库并调用原厂初始化，随后扫描该外部卷目录；internal 路径跳过初始化。`run_idle_maintenance` 也遍历外部卷，并非免扫描入口；`scan_file` 不走该调用。静态核查不能排除反射或其他组件的间接入口，目前未发现只初始化默认铃声而不扫描外部文件的现成公开调用。

### 0.3.0 新实例默认铃声自动设置（2026-11-08）

- 采用新实例正常外部存储卷，调用目标固件原厂 MediaProvider 的 `scan_volume external_primary`，不再使用独立空卷或额外启动。RedMagicEMU 在目标固件成功导入、发布前建立一次性标记；目标 Guest 的内置铃声索引完成后，检查外部卷可用并持久消费标记，再触发原厂扫描，读取三个默认铃声 URI、音频分类和 `_set` 标志。旧实例无标记，不会发起首次默认值扫描；扫描错误、超时或异常中断后不自动重放。一次性标记不是用户输入或后台写入的互斥锁，首次准备期间并发设置铃声的行为尚未验证；部分失败需在 Guest 内手动选择铃声。
- clone 包 `1d2e77bdf71a7b0403c92172d538fbf81665512defb4f0f306a57a4b211ab714` 已安装并核对设备摘要。既有实例正常关机、更新并启动后，三项铃声 URI 与更新前逐字一致，日志仅显示系统目录索引。删除该 clone Guest 后，从原始固件重新导入新实例；发布后检查标记存在，首次启动后日志显示三项默认值已验证，标记进入完成状态。只读查询确认三个 URI 分别解析为原厂 `1Alpha_5.ogg`、`1Bounce.ogg` 和 `1Quantum_Foam.ogg`，三个 `_set` 均为 1；完成原厂引导后，虚拟来电响铃。本次未手动发起媒体扫描或修改铃声设置，虚拟来电记录留在 Guest。
- 新实例正常关机重启后，日志报告 41 秒启动完成、系统音频索引成功，三项默认 URI 与标志和重启前一致，首次扫描完成标记仍在，重启日志没有再次报告首次默认初始化。以上是此设备上的一次导入与重启结果，不证明所有宿主文件系统实现或异常中断行为。默认外部卷可能映射宿主共享目录，原厂扫描会扫描该卷；当前范围允许采用此行为。
- 标准版单元测试 412 项，失败 0、错误 0、跳过 51；标准版及 clone Debug 构建成功。新实例铃声通过模拟音频与宿主桥播放。
- 同一全新实例重启后再次通过宿主菜单触发虚拟来电，实机可听到铃声；测试来电记录留在 Guest。

### 0.3.0 铃声中断恢复与录音实测（2026-11-08）

- 根因：旧逻辑在只读卷就绪检查前就永久领取一次性标记，任何检查失败或后续核验中断都导致重启后不再处理。现对已领取实例增加只读恢复：重启后只读取并核对三项默认 URI、音频分类和 `_set`，都匹配才补记完成；不会重新调用外部卷扫描或修改 Guest 铃声。完成标记 I/O 错误不伪报成功。卷尚未就绪、扫描失败、超时或部分写入而未匹配的场景仍不能自动重扫，避免覆盖用户随后选择的铃声或静音；首次准备期间并发更改铃声也仍未验证。
- clone APK `6b000e43ab2f58459e52334047b44cf7f592806ad7f324e5f240665a0ee5c606` 已安装并校验设备摘要。将当前 Guest 已完成标记暂存，模拟“实际设置成功但完成记录中断”，正常启动后系统音频索引完成并记录默认值核验成功；新完成记录与暂存的原记录逐字相同，临时备份已移除。该次是对同一设备的受控状态模拟。
- 经设备权限界面授予麦克风权限后，Guest 原厂录音机录制约 12 秒并回放，实机听感确认录音正常。宿主日志记录为 Guest 采集 48 kHz 双声道音频，说明此次录音通过模拟音频与宿主麦克风桥接；录音机列表中这段测试录音已通过原厂界面删除。Guest 录音机请求电话权限并经界面允许；宿主 clone 麦克风权限现已授予。拒绝权限、重复录制、其他应用和长期稳定性尚未实测。
- 标准版单元测试 421 项、失败 0、错误 0、跳过 51；标准版和 clone Debug 构建成功。本轮音频实测覆盖宿主桥接的录音和播放。

### 0.3.0 铃声自动重试与短信通知实测（2026-11-08）

- 首次初始化失败后的策略优先自动恢复原厂默认，并接受失败期间新选择的铃声可能被恢复默认。将外部卷只读就绪检查移到扫描资格领取前：检查失败或取消仍可在下次 Guest 启动重试。原厂扫描完整返回后，若结果核验未完成，同一启动内间隔 5 秒最多尝试 3 次；扫描超时、输出不完整或中断时不在同一宿主启动期间盲目重发。缺少宿主 boot ID 时保留一次首次扫描的兼容回退。
- 对已发出且结果不明的扫描，持久记录首次宿主 boot ID；只在读取到有效且不同的宿主 boot ID 后允许下一次扫描。同一宿主 boot 的恢复由持久独占记录防止重复执行；旧版无基准的进行中状态第一次只记录当前 boot，之后真正宿主重启才可重试。该机制不重启手机，不能将普通 Guest 重启当作旧任务已结束；读取不到 boot ID 或记录损坏、无法同步时保持未确认。已明确的自选铃声或静音通过原厂标记和设置行区分，保留时不误报为三个默认值；无法解析的内部媒体行不判成功。首次准备期间的并发设置仍非原子操作。
- 已安装并核对 clone APK `5a605af79d803a92861ff723544a1f9cb5d15c0ae69988d9d295370bcb712e16`。在正常停止 Guest 后，仅临时备份首次扫描控制记录、保留 Guest 数据，模拟此前就绪检查未开始扫描的状态；重新启动后设备日志显示原厂默认卷扫描和默认值核验成功。再次正常停止 Guest，临时删除其来电默认设置及标记并以测试专用的不同 boot ID 模拟旧宿主启动，重新启动后自动触发原厂扫描，来电默认 URI 和标记恢复，通知及闹钟 URI 保持原值。测试后恢复实际基准记录并移除临时文件。该次没有真实重启手机，不能当作跨真实宿主重启的实机证明；扫描命令本身超时或崩溃的真实注入仍待实测。
- 通过宿主 Fake SMS 发一条测试消息，Guest 中出现短信通知，实机听感确认通知声；测试短信留在 Guest。测试前发现 Guest 为静音模式，为听测经原厂快捷设置切换至响铃，测试后恢复静音；未更改宿主勿扰或硬件策略。通知通过模拟基带、模拟音频和宿主桥接实现。
- 标准版单元测试 463 项、失败 0、错误 0、跳过 51；标准版和 clone Debug 构建成功。闹钟实际触发、普通媒体格式及长期稳定性待设备检查；手机跨真实宿主重启的恢复与并发操作仍待实测。
- 最终边界修正后重新完成标准版测试与标准版、clone Debug 构建：465 项、失败 0、错误 0、跳过 51；最终 clone APK `14093e92325d9cd07126d68cf8e3e2cf7ed5697b48d7f8c116e8b085e6b97591` 已覆盖安装并核对摘要。最终包正常启动后，三项原厂铃声 URI 仍在、Guest 静音模式仍保留，日志未重复首次默认扫描。前述两次模拟恢复和短信听测使用上一测试包 `5a605af7…`；最终包新增失效 URI 与并发复核分类的修正，已覆盖单元测试，但该两项边界未在手机重复模拟。

### 0.3.0 本地 WebView 检查（2026-11-08）

- 已有稳定启动证据限于单台设备的导入、重启和数分钟观察；本轮运行的 clone 包摘要仍为 `14093e92325d9cd07126d68cf8e3e2cf7ed5697b48d7f8c116e8b085e6b97591`，运行日志报告 Guest 39 秒启动完成。长期和其他宿主的稳定性尚未验证。
- 操作：通过 RedMagicEMU 的安装入口，在 Guest 安装不申请额外权限、不联网的自造应用，加载内置 HTML 并点击页面按钮。预期文字完整显示、JavaScript 更新页面、点击后显示反馈。Guest 当前选择的系统 WebView 是 `com.google.android.webview` 140.0.7339.207；显示经 Guest 合成和模拟器宿主显示桥呈现，触摸经宿主输入桥送回 Guest。未向 Guest 直通宿主特权节点，也未修改原始固件。
- target SDK 36 测试中页面显示 JavaScript 成功和按钮点击反馈，但页面顶部被标题栏遮挡，原生顶部状态也不可见；同布局的纯原生对照同样遮挡。诊断记录 root 从屏幕 y=0 开始，顶部状态高 78 像素，报告上方系统 inset 为 125 像素。只更改包名和 target SDK 为 28 的对照中，root 从 y=307 开始，原生及 WebView 顶部均完整显示，WebView 的静态文字、脚本和按钮反馈均实测成功。该对照证明现象与目标版本有关，不足以确定是探针未适配窗口边距、目标固件窗口行为还是其他兼容问题；没有对产品做未经证据支持的修复。
- Android 16 对 target SDK 36 强制使用 edge-to-edge，应用需处理可能的系统栏遮挡。修改自造探针，改用无标题栏主题并按系统栏和切口设置边距；重新安装后，target 36 的顶部状态、页面完整文字、JavaScript 和点击反馈均在手机屏幕上实测成功，记录的顶部边距为 125 像素。这同时改变了主题与边距，不能单独证明旧探针遮挡的全部根因。
- 新增可重建的离线 Guest 检查程序 `tools/diagnostics/webview-local`，同时保留纯原生对照，供后续复测。标准版单元测试实际重跑 465 项、失败 0、错误 0、跳过 51；标准版和 clone Debug 构建成功（构建任务复用已有产物）。只覆盖本地页面和一次点击；旧探针的遮挡原因、远程网页、视频、真实普通应用、长期稳定性和原厂图形 HAL 完整兼容待设备检查。
- 三个自造 Guest 探针均已卸载，手机下载目录中的本轮测试 APK 与实例专用命令输出已删除。电脑系统临时目录中的本轮测试构建、脚本和截图仍留在临时目录；截图可能包含屏幕上原有的通知内容，未加入仓库。

### 0.3.0 本地 H.264 视频播放（2026-11-08）

- 前提：已有单台手机稳定启动的短时实测。本轮更新前 clone 的 Guest 启动日志为 39 秒；预期本地 H.264/AAC 视频能显示连续变化的画面、播放声音并正常结束。初测 640×360、24 fps、12 秒素材时实机有声音，但两个时刻的视频区域均为黑色，播放进度停在 12011/12021 ms，未收到结束回调。手机中选中 `c2.android.avc.decoder`；相关日志重复报告 `lockYCbCr` 错误与 AVC 图形缓冲映射失败。相同素材在电脑可完整解码。
- 分层复现：普通 Guest 应用使用 AHardwareBuffer 对照，`YUV_420_888` 的 CPU 读写可成功；目标软件 AVC 解码器的运行库反汇编确认其申请 YV12、宽度按 128 对齐、CPU 读写，且独立测试中 YV12 分配成功但锁定返回 -38。实际加载的 64 位 gralloc 资产与仓库中已有源码不同：旧资产缺少 YV12 注记写入及相应平面描述；重新构建现有实现后，YV12 三平面锁定和解锁在设备上成功，播放器也可结束，但画面仍黑。版本号门控的早期猜测被实际 mapper 反汇编否定，没有按该猜测修改。
- 第二处路径：设备的 GPU 桥已启用，实际使用的 GLES 资产与仓库旧资产摘要一致；旧资产的图像上传路径只接受 RGB 格式，不能上传 YV12。为 API 36 的 Guest 桥补充 YV12 平面到 RGBA 的转换，再通过现有宿主图形桥上传；CPU 写入更新共享缓冲的代数，以便重复使用缓冲时更新画面，并避免将 YV12 标记为 RGB GPU 回读。转换目前采用 BT.601 limited，支持范围与资源上限以源码为准。
- 安全与回归：YCbCr 锁在读取共享注记前检查 fd 当前长度、非零 offset、framebuffer 标志，并对注记尺寸、步幅、平面与保留区做界限检查；删除重建源码中原有的详细地址调试文件写入，出错仍返回失败。新增宿主 C 回归覆盖正常共享映射、损坏注记、短文件、错误 offset、framebuffer 及写入代数，以及 YV12 色彩、步幅和越界情况；`GRALLOC_YUV_HOST_OK`、`YV12_RGBA_OK` 通过。当前长度检查不证明能抵御其他进程在检查后并发截短 fd，恶意句柄经实际跨进程传递的完整边界仍未验证。
- 最终 clone APK `30418217f8bc50c1c512f0e183f34364716d4d65acc1831da7dbebd5c6efa739` 覆盖安装原 clone，保留 Guest 数据；设备摘要一致，Guest 在 38 秒报告启动完成并显示桌面，图形桥启动成功。运行副本中的 gralloc 与 GLES 资产摘要分别为 `b1081a1c60b9071b37e5860e7e0889fd1f3dc74c92070ad832c8114b7e81924b`、`f262f7b95ed1125b3549f9b9fcdff2ca6041ea24a8cf250cff144aeb749f3e41`，与构建资产一致。最终包对两种 YUV 及两种读写用途的分配、锁定和解锁均返回成功；同一测试视频在不同时间显示不同彩色帧并正常结束，画面和提示音均正常。系统播放器也能播放该素材，并有画面截图；该项应用识别依据实机验收。测试中通过 Guest 权限界面确认系统视频应用的媒体访问；事后查询 `com.zte.videoplayer` 的音频和视频读取权限为已授予、图片读取权限未授予。
- 标准版单元测试实际运行 465 项、失败 0、错误 0、跳过 51；standard 和 clone Debug 独立目录构建成功。两项 NDK 源码到资产一致性检查通过；`tools/diagnostics/video-local` 与 `tools/diagnostics/ahb-formats` 为可重建的 Guest 回归程序。旧 GLES 资产无法由当前 NDK 和已检查的 Git 历史源码逐字重现：此次重建还包含仓库源码中早已有而旧资产未包含的其他图形变化；桌面与触摸已复测，其他图形场景仍需回归。
- 回滚须仅撤回本轮 gralloc/GLES 源码与对应 API 36 资产，再重新构建、覆盖安装 clone 并正常重启 Guest；旧两项资产摘要分别为 `b04317cb783ed3745403fa35a379604ce729a75a9c75b07b4cbba58a86249ba2` 和 `e4f623d4c066487947d2f3fde61b38814b773fe07c25c35735d846fa3bd0c032`。原始固件未修改；不直通宿主特权节点。Guest 临时测试应用和本轮手机传输文件已删除。电脑临时目录中的固件组件只读分析副本 `rme-c2-softavc-evidence-20261108.so` 仍留在系统临时目录，未加入仓库。

### 0.3.0 振动桥排查（2026-11-08，尚未实现）

- 操作与预期：在已启动的目标 Guest 中发起 500 毫秒普通振动请求，预期手机产生短振动。当前 clone 沿用此前实测启动的视频版本；Guest 的 `vibrator_manager` 未列出振动器，命令请求记录为 `ignored_unsupported`，实机确认手机没有振动。宿主系统列有振动器，单独发起宿主 500 毫秒请求后实机可感知振动。此对照表明本次宿主振动通路可用。
- 路径核查：现有 `Sensors.kt` 对旧版 Guest 的 `timed_output` FIFO 提供宿主振动调用，当前 Android 16 Guest 未发现可用的振动 HAL，不能使用这一旧入口代替经过 Guest 系统权限检查的新桥。旧 FIFO 可由 Guest 写入。
- 安全阻碍：当前模拟 Binder 接收客户端上报的虚拟 UID 和 PID；Guest 虚拟 `setuid` 路径无条件更改 UID，不能仅以 `Binder.getCallingUid()` 或上报 PID 证明请求来自已通过权限检查的系统服务。此处为调用链源码检查。仅在宿主认证 HAL 进程也无法证明 Guest 请求已通过应用权限检查；需要建立和验证可信的 Guest 调用者身份及系统服务授权链，再实现受控的宿主振动请求与取消。该改动涉及共享启动及 Binder 路径，不能当作简单可靠的小修。
- 本轮没有接入或安装振动桥，没有修改原厂固件或直通宿主硬件节点。尚未完成桥接功能、权限拒绝和隔离回归、构建及新实现的设备验收；也未验证原厂振动 HAL、RichTap 或完整振动效果兼容。
- 手机侧本轮诊断输出已清理。电脑上的旧实验代码未接入产品，其中对虚拟 UID 可信性的假设不成立；目录仍在 `native/gueststubs/vibrator/`，已标注禁止将该实验接入。电脑隔离环境中的身份伪造动态复现尚未完成。这是当时的实验记录，当前普通振动桥的接入及验收见本文件后续记录。
- 参考 AEmulator-Sunset `2b99b5aaedef9ff6baa352a9049c904b97cf1c6f`：其 32 位旧版 Guest shim 把旧振动接口请求经本地 socket 交给宿主，再由 Android Vibrator API 执行；宿主使用固定长度消息、限制单次时长，并在停止时取消振动。可参考其协议和生命周期设计，但该旧接口不是目标 Android 16 的振动 AIDL HAL；其 socket 服务代码未验证 Guest 调用者身份或振动权限，无法据此证明满足本项目的权限边界，是否存在普通 Guest 应用的实际可达路径尚未实测。源码：[Guest shim](https://github.com/drel4/AEmulator-Sunset/blob/2b99b5aaedef9ff6baa352a9049c904b97cf1c6f/native/guestshim/vibration.h)、[宿主桥](https://github.com/drel4/AEmulator-Sunset/blob/2b99b5aaedef9ff6baa352a9049c904b97cf1c6f/app/src/main/java/app/aemu/core/VibrationBridge.kt)、[socket 服务](https://github.com/drel4/AEmulator-Sunset/blob/2b99b5aaedef9ff6baa352a9049c904b97cf1c6f/app/src/main/java/app/aemu/core/UnixServer.kt)。本次仅源码参考，未引入上游代码或实测上游。
- 本轮结束手机侧操作：目标 Guest 正常请求关机，运行日志记录停止并清理 Guest 进程，随后断开设备连接。电脑端新增未接线的宿主振动会话控制组件，限制单次 1–5000 毫秒，串行化振动和停止、拒绝失效会话并传播宿主调用失败。停止尚在等待锁时其他已排队请求可能先执行；取消失败时，马达状态未知。该组件不负责校验 Guest 身份或权限，不创建通信入口，也没有调用手机振动。
- 离线实验核对已有 AIDL v2 接口快照及摘要，用 SDK 36 生成接口并编译未注册的实验 HAL；检查未支持效果、无效设备编号和本机测试桩拒绝身份时的行为。身份拒绝来自测试桩，既有 HAL 的 UID 检查仍不可信，目标固件实际接口及版本、服务注册和运行权限均未验证；实验不会生成安装包或接线。电脑完整标准单元测试 471 项、失败 0、错误 0、跳过 51；standard 和 clone Debug 构建成功。Guest 离线实验构建通过，Java 8 目标及 Android SDK 工具版本有警告；未安装这些构建，也未再次操作手机。振动桥仍未完成，实机验证待手机可用时进行。
- 后续仅电脑推进：Guest 振动实验移除按 Binder 上报 UID 放行的逻辑，默认拒绝远程事务和振动请求，亦不报告振动器；离线测试桩即使返回系统 UID 也不能触发测试接收器。目标 Guest 的正常启动代码不再启动无认证旧振动 FIFO，也不在停止时写该路径；加速度通道保留，旧节点不删除。此决策不再依赖可能缺失或错误的 Guest API 元数据；单元测试验证回调和普通测试文件行为，但未证明既有旧读取线程已退出，未在真实 FIFO 或手机实测。
- 宿主侧编写受限振动帧及接收代码，复用 pidfd、socket 和逐包内核凭据检查，按 epoch、递增序号和长度校验后，将有限时长振动或取消操作送到会话控制器；异常时会关闭会话并保留取消失败。该检查只确认宿主监管的服务进程，不证明原始 Guest 应用已通过权限检查。单独的本地 socket 回归、两项现有 Radio native 回归通过；SDK 36 的实验 AIDL 构建及默认拒绝检查通过。标准单元测试 479 项、失败 0、错误 0、跳过 51；standard 和 clone Debug 构建成功，存在原有 SDK/弃用警告。
- 使用 NDK 单独构建临时宿主 so 并核对新增振动 JNI 及原 Radio JNI 导出；未覆盖现有 `libaemuhost.so`，本次 APK 因而没有新增 JNI 实现。宿主接收组件尚未创建连接或接入 GuestVm，缺少 socket 的实际所有权、进程退出及停止时清理；Guest 应用身份和振动权限的可信链也未建立，所以不能启动实际振动或称桥接完成。本轮未操作手机，真实 Guest 权限、设备振动和启动回归均待后续测试；原厂 HAL 和 RichTap 未验证。
- 后续仅电脑继续：补充未接线的振动会话接收与清理，拥有传入的 socket 描述符；接收报错或关闭时停止会话并释放输入，关闭标记生效后丢弃尚未分发的请求，等待清理时被中断会在清理完成后恢复中断并报告。接收与关闭串行，失败会保留供调用者在关闭时读取。JVM 回归覆盖模拟输入失败、关闭竞态、中断和清理失败；未验证真实 Android 描述符、真实进程退出或后台错误主动通知，也未启动产品通道。
- 使用 NDK 28.2.13676358 验证：不包含新振动源文件时，当前源码不经 strip 重建的旧 `libaemuhost.so` 与原资产逐字一致，SHA-256 为 `c87781168db2b8435ead056ff2a9fe8698b90edbfe2d418f855d26be35a6f0a9`。加入振动源后以同一方式重建并更新资产，新 SHA-256 为 `7e40de7e9b926026851949d811f258a25dcdc1cca1bd56287c3d5dd0d146f376`；旧导出和动态依赖保留，新增振动 JNI 和内部函数。旧资产已备份到系统临时目录 `rme-vibration-prechange-libaemuhost.so`，回滚可用该摘要一致的备份恢复，再重新构建 APK；原始固件未修改。新库链接布局发生变化。
- 最新 standard 与 clone Debug 构建均成功，APK 内宿主库摘要均与新资产一致并包含振动 JNI；Gradle 提示无法剥离该库，按原样打包。标准单元测试 486 项、失败 0、错误 0、跳过 51；振动原生帧测试及 Guest 实验离线检查通过。Guest 应用身份与权限数据仍未形成可信链，真实 Guest→宿主链路、Android JNI、振动和既有功能运行回归均未完成；新构建未安装，手机未操作。
- 继续在电脑核查 Guest 授权：当前虚拟 Binder 采用请求方上报的 UID/PID；zygote fork 后代没有由宿主独立认证的 UID 分配记录，通用 Guest 文件访问也未证明系统权限数据受到独立隔离。现有 PackageManager 数据或 APK 清单不能直接作为可信授权结论；源码不足以证明普通 Guest APK 已能触达相关路径，未进行攻击复现。实验 HAL 仍保持不声明、不注册和默认拒绝。
- 新增仅运行自造进程的 `tools/diagnostics/parentage`：以内核消息凭据、父子 pidfd 和 `/proc` 观察物理父子关系，核对存活、关系及子进程退出，Linux 本机结果为 `PARENTAGE_DIAGNOSTIC_OK`；NDK 28.2 对 Android ARM64/API 29 目标交叉编译通过，未在 Android 运行。先前设备的 pidfd 与 Guest 内凭据试验不能替代此组合的设备测试。该观察只证明本机当时的物理进程关系，不证明 UID 分配、zygote 完整性或权限数据可信，尚未接入产品；振动权限链和桥接仍未完成，本轮未操作手机。
- 继续仅电脑推进身份链：`native/guest64/36/binderd.c` 新增默认关闭的实验严格 PID 模式，使用内核对端身份、对端 pidfd 及逐帧凭据确定物理发送进程，能力探测失败时拒绝启动；原模式仍保留。模式只认证物理 PID，不认证 Guest UID、应用权限或 Binder 载荷的完整安全。Linux 本地正常连接、连接关闭、缺失凭据、进程退出、描述符清理和部分帧边界回归分别通过 `BINDER_PID_GUARD_OK`、`BINDER_PID_DISPATCH_OK`；既有 Binder 事务回归五种模式通过，NDK Android ARM64/API 26 临时交叉编译通过。模式未接入启动流程，旧 `libbinderd64_36.so` 未替换，Android 内核功能和 Guest 启动均未验证。修改前已保存当前未提交源码至系统临时目录 `rme-binder-pid-baseline.c`，SHA-256 `24367fd51187ed222b94269ce39546ff8b9a7fb9b99ba98ff5752c636054ecae`；回滚本轮可恢复该备份并移除新增 PID 校验文件。
- 严格 PID 模式初版存在已知限制：未对齐且无 offsets 的合法 F_BIG 消息会被保守拒绝，文件长度检查后并发截短及 Binder 载荷未完成安全审计；主连接关闭后快速重开和副连接先于主连接处理时也可能被拒绝。后续电脑修订见下，不能根据本机检查启用该模式或声称兼容目标 Guest。
- 新增 `tools/diagnostics/uid-ledger`，只对假设已认证的事件建模普通 fork 与预创建后 specialize 的顺序；Linux 自造事件回归及 ASan/UBSan 检查通过 `UID_EVENT_MODEL_OK`，Android ARM64/API 29 交叉编译通过。事件角色只是未认证的测试字段，系统进程、zygote、预创建流程的可信事件源、执行前屏障、Guest UID 与权限/AppOps 状态均未建立，不得接入正式授权。
- 新增 `native/gueststubs/vibrator/tests/fake_bridge`，自造 Guest 子进程通过真实本地 socket 与现有宿主帧接收器交互，假接收器检查按序开始/取消、时长和关闭，结果 `VIBRATION_FAKE_GUEST_CROSS_PROCESS_OK`；Android ARM64/API 29 交叉编译通过。测试 ACK 和 Guest 发送器仅用于测试，没有注册 HAL 或连接真实马达，不证明 Guest 授权。此次未操作手机，未更新或安装 APK；振动桥仍未接通，原厂 HAL/RichTap 兼容仍未验证。
- 继续修订严格 PID 模式的 F_BIG：对有界大消息先用 `pread` 复制到独立 memfd，补齐未对齐末尾并封住缩短和扩张，再映射副本；短读时拒绝并关闭传入描述符。Linux 直接调用真实 memfd 和生产 `do_txn()` 的测试覆盖未对齐、offsets、附加 FD 顺序、源文件截短后副本可读和短读清理；严格 PID gate、dispatch 和既有 Binder 事务五种模式均通过，NDK Android ARM64/API 26 临时交叉编译通过。该测试不是完整 Binder socket 往返；复制期间源内容可能变化，Binder 对象偏移和内容未完成安全审计，大 scatter-gather、连接代次与正常副连接先到的问题仍未解决。严格模式仍未启用，运行资产未替换，手机未操作；Guest UID、权限链和振动桥仍未完成。
- 后续仅电脑验证 UID 监管候选：`tools/diagnostics/zygote-trace` 只创建并跟踪自造父子进程，以测试专用 seccomp 过滤器截获同 UID 的 `setresuid`，在非 root WSL 上观察到 fork 与系统调用，输出 `SUPERVISION_PROBE_OK`；Android ARM64/API 29 交叉编译通过，未在 Android 运行。它不监管真实 Guest；Android 宿主是否允许此监管仍待验证。当前不以此放行振动。
- 尝试修正实验 Binder 的连接确认和实例代次，但发现严格客户端的 SIGSYS 重入、在途关闭和描述符别名有待查明；本轮握手改动已撤回，四个相关源码及已有测试均与本轮开始前的备份逐字一致，上轮默认关闭的 PID/F_BIG 实验保留。手机未操作、运行资产未替换。范围收紧后暂停继续扩展 Binder 与进程隔离，重新聚焦 Guest 普通振动与宿主桥；真实振动接线和 Guest 权限仍未完成。
- 按收紧后的范围，新增 `HostVibrationDevice`，将已有振动控制器适配到宿主 Android 普通单次振动和取消接口；无振动器时返回不可用，调用失败不虚报成功。该适配尚未创建或连接 Guest 通道。标准版单元测试 487 项、失败 0、错误 0、跳过 51；standard 和 clone Debug 构建成功，未安装或操作手机。实际 Guest 服务识别、请求发送、权限边界和设备振动仍待完成与实测。
- 继续在电脑聚焦普通振动：`native/gueststubs/vibrator/guest_sender.c` 实现对已连接 socket 发送 1–5000 毫秒单次振动与取消，串行序号，失败后退役且不自动重试；不创建连接、不认证调用者、不接入 HAL。发送成功只代表消息入队，失败或关闭后已入队消息仍可能先被宿主处理。Linux 自造跨进程及边界回归输出 `GUEST_VIBRATION_SENDER_OK`，ASan/UBSan 通过，Android ARM64/API 29 交叉编译通过；FD_CLOEXEC 只检查标志，未实测跨 exec。没有操作手机或驱动马达。
- 振动实验目录新增未接线的 ServiceManager 注册代码和离线回归，检查声明、占用、错误和名称可观察状态；同描述符不同 Binder 只能标记归属未确认，不能作为服务身份认证。`verify_offline.py` 的 HAL 默认拒绝与注册回归通过，产生的仅为临时编译产物，不生成安装包、不注入 VINTF 或注册 Guest 服务。当前 v2 离线接口与目标固件实际运行类版本的兼容性未知；目标 Guest 尚未识别振动器，发送端也未接入 HAL 或宿主，应用权限和设备行为尚未验证。
- 继续离线核查原有振动入口：Android 16 的运行副本采用的两份 vendor 主清单资产均无 vibrator 声明；在具有自带 vendor 的固件上，准备副本时还会按配置移走未保留的原厂 vendor 和 ODM 清单片段。这与 Guest 未发现振动器的实测一致，但仅凭仓库资产不能确定目标固件原有振动服务的版本、是否曾启动或能否复用。启动计划仍可能收集符合条件的原厂 HAL 进程；不能只补清单声明就断定接口可用。未新增服务或放宽权限。本轮重跑发送端 Linux 跨进程回归及 HAL 默认拒绝、注册离线回归均通过；无设备操作，真实桥接及权限仍待验证。
- 原厂优先的电脑只读核查：固件目录 `images/super.img` 的系统指纹匹配本项目目标；它的原厂 vendor 清单声明 `android.hardware.vibrator` AIDL V2 `IVibrator/default`，对应 `vendor.qti.hardware.vibrator.service`，与实验中的 manager 声明并不相同。该镜像和 VM ZIP 中的 super 大小及前 1 MiB 一致，未验证完整内容一致。原厂 RC 指向 RichTap 设备节点，普通振动实现的静态符号与字符串也包含设备节点、ioctl 和 PAL 依赖；策略文件将不超过 3000ms 的 on 请求分配给 OL，更长的分配给 CL，但实际运行分支未验证。Guest 路径转换对不存在于副本的完整 `/sys` 路径可能回退宿主，不能仅恢复声明后直接启动原厂服务；旧 input/FIFO 路径也不能当作已验证的安全振动桥。没有修改固件、启动配置或手机；原厂服务可否安全复用、Guest 权限和桥接仍待确认。
- 对上述镜像中原厂 OL 库作离线反汇编：存在 `LedVibratorDevice::on/off`，其 LED 分支向 `/sys/class/leds/zte_vibrator/` 下的 gain、activate_mode、index、duration_aw、activate_aw 写参数；时长是十进制毫秒加换行，公共写函数还写入末尾 NUL，停止时向 activate_aw 写 0。原厂 RC 使用的 `/sys/class/leds/vibrator/` 路径与库内路径不同，二者运行时关系未确认。构造与分支代码提示可能默认选 LED，但不能排除输入设备分支、CL 分支或服务启动失败；这些均是静态线索，不证明 Guest 实际执行、节点可安全模拟或桥接已完成。未操作设备或原厂硬件节点。
- 继续尝试原厂接口接桥：当前引擎默认拦截 openat 而不拦截普通 write，原厂节点的 open/write/close 不能通过预建普通文件自动转发到宿主。为三个已发现振动 sys 子树增加绝对词法路径限定：即使副本缺少节点也不直接回退宿主；提取生产路径转换函数的电脑回归先复现失败、修复后通过 `VIBRATOR_ABSOLUTE_PATH_FALLBACK_OK`。NDK 28.2 Android ARM64/API 26 引擎临时交叉链接通过，存在原有编译警告，未替换运行资产或操作手机。此限定不覆盖符号链接、相对路径或未知别名，也不证明 Guest 应用权限；真实请求转发与受控进程生命周期尚未接入，不得据此启用原厂服务或宣称完成桥接。
- 原厂普通振动接口继续离线接线：静态核查原厂 OL 库，`LedVibratorDevice::write_value` 为导出符号，on/off 均经 PLT 调用；提供仅研究用的符号拦截器和参数转换器，未绑定会话时拒绝，支持原厂确认的 LED 顺序、1–5000ms 和取消。Linux 中自造 vendor 动态库调用实际 shim、转换器、发送器和宿主帧 gate 的回归输出 `QTI_SHIM_TRANSPORT_OK`；独立转换器和 Linux 符号覆盖回归分别输出 `QTI_LED_ADAPTER_OK`、`QTI_SHIM_LINUX_PLT_OK`。三者组合以 NDK 28.2 临时构建 Android ARM64 共享库并确认目标符号导出；仅验证本机模拟调用，不证明 Android 原厂进程能加载或实际使用该符号。未接入启动流程、未授权 Guest 应用、未使用手机。中途停止的多步写入可能使其他线程在取消前持续收到 busy；解绑本身不负责取消宿主振动，正式接线前仍需解决会话清理、Guest 权限和路径隔离，不能称已接通或兼容原厂 RichTap。
- 参考 Sunset 继续在电脑接原厂接口：未完成的多步振动请求现在可由新请求接替，旧请求不能沿用新时长，并发时旧请求可能失败。实验会话由 Guest 端主动连接，接收带内核凭据和 epoch 的宿主握手后绑定适配器；关闭时尝试发取消并清理连接。此处使用 `recvmsg`，因为当前 run64 会改写 `SO_PEERCRED` 的 UID；源码显示默认过滤列表未拦截 `recvmsg`，但 Android/Guest 下行为未实测。Linux 真实进程回归覆盖普通振动、关闭取消、错误握手与身份、额外 FD 清理及宿主断开，输出 `QTI_SESSION_OK`；转换器与模拟原厂调用到宿主帧 gate 回归分别输出 `QTI_LED_ADAPTER_OK` 和 `QTI_SHIM_TRANSPORT_OK`。NDK 28.2 Android ARM64/API 29 临时共享库构建通过。宿主还未实现对应握手接线，原厂进程也未加载适配；Guest 应用权限、完整路径隔离与真实设备均未验证，因此不得将实验会话启用为产品功能。本轮未操作手机，未更新运行资产。
- 随后补充默认未启用的宿主 `HostVibrationChannel`：接收时核验受监管进程与 pidfd，发送 RMEH 握手后交给现有振动接收会话；关闭或失败尝试清理会话。电脑侧又补了仅实验用的 Guest 预加载初始化，Linux 自造进程验证握手、模拟原厂短振及正常退出取消，结果 `QTI_AUTOLOAD_OK`；NDK 28.2 Android ARM64/API 29 临时共享库构建通过。标准单元测试 489 项、失败 0、错误 0、跳过 51；standard 与 clone Debug 构建成功，有 SDK 工具版本提示。宿主连接类仍未接入 GuestVm，适配库未安装；Android socket、Guest 动态链接、进程退出清理和应用权限均未实测。尤其不能直接把预加载环境变量传给当前宿主启动器，否则适配器可能在错误进程提前执行。手机未操作，未声称振动桥或原厂 HAL/RichTap 兼容完成。
- 继续在电脑处理加载顺序：run64 源码新增默认关闭的实验选择，仅在明确开关、64 位及原厂 vibrator 服务精确路径匹配时，才向 Guest 初始栈添加固定适配库；检测到既有 LD_PRELOAD 则拒绝覆盖。正常 `do_exec` 传播原样环境时移除该适配项和 RME 振动参数。策略函数测试输出 `VIBRATOR_PRELOAD_POLICY_OK`，路径和预加载自造进程回归分别输出 `VIBRATOR_ABSOLUTE_PATH_FALLBACK_OK`、`QTI_AUTOLOAD_OK`；NDK 28.2 ARM64/API26 引擎临时构建通过，有既有编译警告。该测试未运行 Android linker，不覆盖原厂进程改写或拼接 LD_PRELOAD、fork/execveat 与所有别名；运行资产未替换、实验开关未启用。源码核查再次确认 Binder 应用身份来自 Guest 上报，无法仅靠认证原厂服务证明应用权限，仍未建立安全接线；未操作手机。
- 当前范围不因 Guest 应用授权问题阻塞普通振动，保留宿主普通振动接口及不直通宿主特权节点的要求。电脑端新增适配库的 NDK 构建脚本和仅对指定固件显式启用的运行副本准备器；原始固件不变，遇到不同内容的同名文件或符号链接拒绝覆盖，当前不支持不同版本自动替换。重建库的 SHA-256 与资产均为 `a0bf4a92e68d5390634603b85fef88215dcc97ce32783e1b11d91cb13dbbfb11`，且 standard Debug APK 索引及数据包含同一资产；准备器未接入 `TreeFixer` 或 `GuestVm`。标准单元测试 493 项、失败 0、错误 0、跳过 52，其中运行副本准备的 4 项有 1 项因 Windows 不支持创建符号链接而跳过；standard 和 clone Debug 构建成功。静态核查发现原厂服务还含 InputFF、PAL 等访问分支，现有路径限定未覆盖所有别名、符号链接和 `openat2`，无法确认不会访问宿主硬件节点；本轮没有主动启用原厂振动桥或恢复声明，但现有启动计划可能调度原厂服务，实际运行状态未核实。未操作手机；Guest 应用权限真实性仍未验证。
- 打包核查补充：适配库通过现有 EngineAssets 索引访问；运行副本准备器发布新文件使用硬链接，手机文件系统和 Android 应用环境是否支持尚未验证。上述准备器目前没有生产调用，不能据其测试声称用户新实例已自动安装。
- 继续仅电脑核查宿主节点隔离：用原厂振动 RC 等价字段与自造空白程序运行 `InitPlan`，文件存在时服务会进入启动计划。新增仅供研究的 Landlock 子进程测试：在 WSL 临时目录内，允许 Guest 子树读写，拒绝子树外绝对路径、外部 dirfd 相对路径、越界符号链接和 `openat2`；输出 `VIBRATOR_LANDLOCK_GUARD_OK` 与 `VIBRATOR_LANDLOCK_OPENAT2_OK`，NDK ARM64/API29 仅对象编译通过。测试同时确认限制前已打开的外部 FD 仍可写，因此必须另行处理；元数据操作、必要设备白名单、Android 内核支持和原厂服务兼容均未验证。该研究未接入产品或启用桥，未操作手机。
- 对上一实验补充已打开 FD 的处理：在自造单线程子进程中通过 `close_range` 关闭除明确保留的 Guest 根目录和 0–2 以外的描述符，失败须停止；电脑测试验证根目录和标准描述符保留、根描述符两侧的外部 FD 关闭，Landlock 和 openat2 回归仍通过，NDK ARM64/API29 对象编译通过。保留的标准描述符是否可信、其他线程、必须继承的描述符、元数据操作、设备 ioctl 与 Android 支持仍未验证；该诊断没有接入产品，不能称原厂服务已隔离或振动已接通。
- 继续电脑验证隔离与通信：清理继承 FD 并仅放行自造 Guest 根后，WSL 子进程仍可通过根外的文件系统及 abstract `AF_UNIX SOCK_SEQPACKET` socket 与自造服务双向通信，输出 `VIBRATOR_LANDLOCK_UNIX_SOCKETS_OK`，同时重新通过 openat2 与目录隔离回归；此结果来自离线 socket 对照。静态核查原厂服务同时依赖普通振动实现与 RichTap，并包含 input 枚举、校准文件和额外 sysfs 检查；实际调用路径及安全运行所需白名单无法由静态结果确定。未操作手机，隔离和桥仍未接入产品。
- 继续检查启动接线时修复握手等待不一致：宿主 attach 最长等待 20 秒，而 Guest 先前只等 2 秒；自造进程延迟 3 秒发握手时旧版失败，Guest 有界等待改为 25 秒后 `QTI_SESSION_OK` 通过，自动加载回归 `QTI_AUTOLOAD_OK` 通过，未测试完整 25 秒超时或 Android 调度。用 NDK 重建实验资产，新 SHA-256 `09a06971e8ef3a8b7d503ca1d632f93b882ad9c8c34b2c02dbd90984d1217329`；旧资产摘要 `a0bf4a92e68d5390634603b85fef88215dcc97ce32783e1b11d91cb13dbbfb11` 已备份在系统临时目录 `rme-vibration-before-delay25-a0bf4a.so`，需要回滚时核对摘要后恢复旧资产并重建 APK。标准单元测试 494 项、失败 0、错误 0、跳过 52；standard 和 clone Debug 构建成功，有 SDK 工具版本提示。原厂服务及宿主通道仍未接入产品启动；此前研究的隔离没有覆盖全部设备访问、FD 和元数据场景。未操作手机。
- 振动启动准备进一步接线：`GuestVm` 的正常启动会为指定目标尝试在运行副本准备适配库，Recovery 不执行；文件冲突或准备失败保留原文件、记录日志且不阻断 Guest。Android 环境的硬链接发布尚未实测，不能声称安装一定成功；不同版本资产仍拒绝自动覆盖。`GuestRunner` 的五项振动专用环境键只接受宿主显式传参，过滤固件、用户及派生环境对这些键的注入或覆盖，其他变量优先级不变。自造进程验证启动器显式实验模式中先认证、再安装有限 Landlock 规则及清理无关 FD，正常启动协议不变，输出 `VIBRATION_LAUNCHER_TEST_OK`；实验模式的回执仅表示有限规则已安装，未覆盖全部元数据、设备和 Android 依赖，不能作为安全启用证明。临时交叉编译通过，应用内 launcher 资产未替换。标准单元测试 499 项、失败 0、错误 0、跳过 52，standard/clone Debug 构建成功；本轮未操作手机。原厂服务隔离与振动桥仍未接入或实测。
- 后续电脑接线：运行副本准备现返回布尔结果，仅目标文件创建或原有内容核验一致时返回 true；Recovery、非目标或准备失败返回 false，失败记录日志且不阻断 Guest，此结果仅代表文件准备而非隔离或振动可用。启动器的显式实验模式增加 seccomp 限制若干 chmod/chown/utimens/xattr 元数据操作；自造进程回归验证实验模式拒绝这些操作、普通模式不受影响，输出 `VIBRATION_LAUNCHER_TEST_OK` 和 `VIBRATION_METADATA_GUARD_OK`，NDK ARM64/API29 临时链接通过。实验尚未约束所有新 syscall、经 SCM_RIGHTS 传入的 FD、Guest 目录内设备或挂载，亦未验证 Android 和原厂服务；回执不能作为完整隔离证明，应用内启动器未替换，振动桥未启用。标准单元测试 499 项、失败 0、错误 0、跳过 52，standard/clone Debug 构建成功；未操作手机。

- 振动桥电脑端继续接线与隔离实验：自造受控进程经实验启动器握手、Guest 原厂接口模拟调用、适配器和真实宿主帧 gate，验证 500 毫秒请求、取消与连接关闭后的取消，输出 `VIBRATION_FULL_CHAIN_OK`。这不是实际原厂服务或 Android 动态链接器，也未调用 Android 宿主振动 API，不能当作手机振动成功。目标引擎的文件描述符接收与 Binder 大包拒绝逻辑通过电脑回归；拒绝大包会终止目标进程，可能影响原厂服务，Android 的多线程、运行依赖和其他取得描述符路径仍未验证。
- 实验启动器仅针对显式实验模式允许经设备号核验的 `/dev/null` 读写、`/dev/urandom` 读取和经 procfs 校验的本进程 `/proc/self` 只读；电脑回归拒绝外部文件、其他进程 `/proc`、`/dev/zero` 与 `/dev/urandom` 写入，输出 `VIBRATION_LAUNCHER_TEST_OK`。这仍不是完整隔离认证：Guest 根目录内设备或挂载、其他文件描述符来源、Android 内核与原厂依赖尚待核查，不得据握手回执认定安全。当前范围允许不以 Guest 应用授权真实性阻塞普通振动，但仍禁止直通宿主特权节点；没有修改宿主硬件节点。
- 已构建并打包独立振动引擎 `libaemu64_vibration_36.so`，SHA-256 `88945ca0bba34f8dff5410c75f15ea6b04ac5ce277673d99cb9ac48720088325`；普通引擎 SHA-256 `72767cb685adcb2f88ce29147fc4ec2feab79495e19dbecaa99561f3f93818ce` 保持不变。显式实验选择仅匹配目标固件和原厂振动程序，缺失或不可执行时拒绝回退普通引擎；电脑定向测试 4 项通过、1 项因 Windows 不能撤销可执行权限而跳过。Android 中引擎提取、执行和原厂服务通过适配器调用仍未验证。
- 启动器已拆分：普通 `libaemu-launch.so` 恢复至此前版本，SHA-256 `b537ab1c5fb079a42b811ec860d2bb1a0d3d3b7cc917ba32f746f32671e4b9f0`；实验启动器另存为 `libaemu-vibration-launch.so`，SHA-256 `24be29fde68bd393d3cdcdd49adfc8d77d24374691682504979cf571860c4f1c`，实验接线已写入且默认固定关闭。备份 `%TEMP%\rme-launcher-before-vib-v3-098905.so` 的 SHA-256 是 `098905085d141c0eee6e763f10341dff8a4c8951dcb128593729f71286cd37f0`；普通版本另有 `%TEMP%\rme-launcher-before-vibration-b537ab.so` 备份。需要回滚时先停止 Guest 并核对所选备份摘要，再恢复相应资产并重建 APK；不改原始固件。普通启动器恢复后，实验 APK 仍处于本地构建阶段。
- 隔离审查发现具体缺口：在电脑自造私有挂载命名空间中，将宿主 `/dev/zero` 挂入模拟 Guest 根目录后，实验启动器返回 `I`，受限进程仍可写该设备；即使移除设备 ioctl 权限，写入仍可成功。此结果证明现有回执不能阻止根目录内已有设备或挂载的访问，故不得启用实验振动桥；这是自造进程测试，并非对手机或原厂服务的设备实测。本次未操作手机。

- 默认关闭的实验接线已限定原厂振动服务使用独立引擎、受控启动器和宿主认证通道，包含失败与停止清理；不提供外部开关，不会启用真实桥接。普通启动器恢复此前版本；未知同名声明在关闭实验时保留且不阻断正常启动。电脑标准单元测试 520 项、失败 0、错误 0、跳过 54；standard 与 clone Debug 构建成功，并核对两份 APK 均包含预期的普通启动器、实验启动器和独立引擎。SDK 工具版本及部分二进制无法 strip 有提示；APK 未安装，新版稳定启动与真实振动未实测。
- 新增仅用于电脑自造进程的 nodev 与挂载检查实验，输出 `NODEV_NAMESPACE_TEST_OK`：字符设备访问被拒、普通文件可读写、嵌套设备和模拟 sysfs 挂载被检出。真实 sysfs、Android 应用创建命名空间的权限与 SELinux、目录并发替换及完整系统调用隔离仍未证明；该测试不接入产品。未操作手机。

- 连接手机后在现有 clone Debug 应用真实 UID、SELinux 与 seccomp 环境运行一次性探针：clone 私有 files 所在挂载报告 nodev，mountinfo 可读，目录句柄经 `/proc/self/fd` 重新打开后 inode 一致；`CLONE_NEWUSER` 返回 EINVAL，`CLONE_NEWNS` 返回 EPERM，`open_tree` 子进程收到 SIGSYS，应用直接读取自身挂载命名空间返回 EACCES。探针不访问 Guest 数据或宿主设备节点，也未挂载文件系统。结果表明该机当前应用环境不支持已测试的私有挂载路线；nodev 与目录句柄本身不能排除并发嵌套挂载。
- 手机中的临时诊断入口与原生探针已从源码及 clone 安装包移除，覆盖安装正常 clone 且保留现有 Guest；重新启动后观察到 Guest 锁屏，日志记录本次 Guest 在 43 秒时报告启动完成；这是一次启动结果，不能替代多次稳定性回归。启动日志显示原厂 `vendor.qti.vibrator` 服务曾启动并以 134 退出，崩溃文本为 `Check failed: status == STATUS_OK`；本机检查原厂服务 ELF 确认 `AServiceManager_addService` 返回非零会进入对应失败检查，因此证据指向服务注册失败，但具体返回码和失败原因仍未知；桥接仍未启用或验收。手机在调试应用启动时显示 16 KB ELF 对齐兼容警告，本轮未修改无关库。普通振动经宿主桥与完整 QTI HAL/RichTap 兼容仍须分别验证。

- 标准 `android.hardware.vibrator.IVibrator` 替代接线方向仅完成离线结构化操作策略和回归：已核对 AIDL v2 的 1/2/3 事务号，对时长、停止、能力与未支持调用作边界处理，本机 ASan/UBSan 测试通过且 NDK ARM64 可编译。此代码没有解析真实 Guest Parcel、注册 Binder 服务或连接宿主，非空回调需求也尚未验证，不构成可用 HAL。该方向若实现，将通过原厂标准接口连接宿主。

- 利用现有 clone Debug Guest shell 执行一次短命的自造 Binder 测试程序，在目标 Guest 的 Java AIDL 代理上确认 `getCapabilities`、`off`、`on(500, null)` 分别使用事务码 1、2、3，固定请求长度分别为 88、88、120 字节，接口标识匹配且调用为同步；测试对象不查询或注册真实服务，不访问硬件。前两项调用方元数据字已屏蔽，原始日志及固件未加入 Git；Binder 驱动对象 offsets、非空回调和完整服务生命周期仍未实测。测试 jar、Guest 输出和设备端临时文件已清理。

- 根据上述 Guest 固定 Java 样本新增独立 Parcel 解码实验，电脑 ASan/UBSan 边界回归通过，NDK ARM64 交叉编译通过；仅接受已观察的同步 SYST 头、固定接口标识及空回调形式，畸形数据和暂不支持的形式拒绝。实际驱动 offsets、目标系统原生调用、Binder 服务注册与回复仍未验证，实验未接入产品。

- 在目标 Guest 使用短命的自造 Binder 接收器验证服务管理器 Java 代理：固定 `addService` 请求为事务码 5、同步 flags，服务名、非空本地 Binder、allowIsolated=false 和 dumpPriority=8 按预期读取，Parcel 对象读取区间为 164–192；这不是 Binder 驱动 offsets 的实测，未向真正 ServiceManager 注册服务。临时探针及设备文件已删除。正在探索由 EMU 现有宿主进程参与 Binder 通信，以提供标准振动接口；尚无真实注册或振动结果。

- 目标 Guest 的自造 Binder 振动代理回复探针验证成功回复：能力查询为 8 字节全零，停止与 on(500,null) 各为 4 字节全零；独立回复编码及 ASan/UBSan 回归通过，异常回复尚未实现。该探针未注册真实服务或访问硬件，临时文件已移除。

- EMU 宿主进程直连 Binder 的独立实验在电脑上与真实 binderd、自造 ServiceManager 往返，覆盖候选注册事务、基础传输、Looper、引用回执和缓冲释放；ASan/UBSan 单元回归与真实守护进程串测通过。该实验只支持单节点及单待释放缓冲；注册对象 flags/stability 和跨进程 offsets 尚未在目标系统确认，且尚未绑定 pidfd、连接 Android 振动 API 或接入产品启动。

- 前述 clone Guest 虽在本次启动时报告完成，但随后宿主记录 `:vm` 于低内存事件中被终止；不能仅凭一次启动完成宣称此次长时间稳定运行。振动测试的 Binder 数据检查因进程退出未执行，后续重新启动再测；这不是振动桥成功或失败证据。

- 使用临时专用引擎和短命 Guest 程序向真实 ServiceManager 注册唯一非硬件测试 Binder：底层事务实测同步 flags=16、一个 8 字节 offsets 项；普通 Binder 的对象偏移为 168、type=0x73622a85、flags=0x113、stability=12。相同测试对象标记 VINTF 后实测 stability=63；该未声明测试名注册返回 IllegalArgumentException，原因尚未确认。测试服务不调用振动或宿主硬件。目标振动服务名比诊断名短两个 UTF-16 字符，其对象偏移 164 由长度推导，并非该目标名跨进程单独抓包。同步标志回归已补充，实际接收的 FD 仍拒绝。

- 离线直接桥单步状态机新增同步 wire flags=16 的真实事务回归，以及停止、重复提交与对端失活等检查；电脑测试通过。该状态机目前尚无真实 pidfd 或 Android 振动调用接线。

- 本次受控 clone 实机测试：Guest 请求持续 2 秒的振动两次，系统均记录 `ignored_unsupported`，重试时手机没有振动；单独的宿主振动此前可感知。诊断发现标准振动接口注册成功后，Guest 的接口描述符查询使实验桥断连；补充该查询后，实机继续遇到事务 10（原语列表）而断连。再补充初始化查询后，实机继续遇到事务 28（PWLE V2 组合长度查询）而断连，尚未修复，不能判定振动已接通。新增初始化查询的本地回归及 ASan/UBSan 测试通过；复审发现当前事务 20、23 在能力位为零时应返回不支持而非成功空列表，待修正。所用设备诊断库仅为临时测试，并未更改原始固件。
- 安全复核发现 Guest 的虚拟 UID 可由客户端修改并上报，且当前直接桥没有可信 Guest 应用调用者校验；仅验证宿主 binderd 身份不足以保护权限边界。受控测试结束后直接桥开关恢复默认关闭，旧 QTI 实验路线也保持关闭；需先解决调用者身份及权限校验，再考虑默认启用。此处为直接桥安全复核。

- 本轮测试结束：Guest 正常关机，已删除本轮设备诊断文件，以默认关闭直接振动实验的 clone Debug 覆盖安装并保留 Guest；提取出的振动库摘要与仓库原有资产一致。新增的事务处理目前仅在源码和临时诊断库中，未重新打包到该资产。标准单元测试 541 项、失败 0、错误 0、跳过 54，standard/clone Debug 构建成功；恢复后的包未再次启动实测。

- 继续受控 clone 实测：修复 Android 16 振动接口的初始化查询及扩展接口空回复后，系统能发现振动器；真实 `on` 请求仍被拒绝，脱敏诊断确认空回调 Binder 的 flags 为 0，而此前只接受 0x13。增加对 0 的支持，其他对象、长度和 offsets 检查保持不变。对应原生 Parcel、step、ASan/UBSan 及本地 binderd 回归通过。
- 同一 Guest 两次请求持续 2 秒振动，分别使用诊断库和去掉诊断日志的库，实机均可感知振动，Guest 均记录 finished；服务在请求后仍可发现。去掉诊断日志的受控测试中 Guest 38 秒报告启动完成，这是一轮启动观察。这验证的是受控测试下标准 Guest 振动接口经 EMU 使用宿主 Android 振动 API 的一次性普通振动；不支持的事务或超过 5 秒的请求仍可能使桥断连。
- 已将源码重建的独立振动库写入应用资产，SHA-256 `ca50f5015565822fdbd7029fbd2385d3ea442902738dfdbe8b63943a15aeff3e`；旧资产 `0b8f2da1b61838aad753eee865abd648501817de2d8eb435f211e1b11bd25d3f` 备份在 `%TEMP%/rme-vibration-before-20261109-0b8f2da1.so`。回滚时先停止 Guest、核对备份摘要后恢复资产并重建 APK。两条实验开关仍为 false：当前 Guest UID 可由客户端伪造，尚不能可靠校验应用振动权限，故不能默认启用或验收发布。标准单元测试 541 项、失败 0、错误 0、跳过 54，standard/clone Debug 构建成功；正常 clone 已覆盖安装、保留 Guest 数据，安装包库摘要与资产一致，默认关闭状态尚未启动复测。未修改原始固件或直通宿主设备节点。

- 当前范围接受 Guest 应用身份和振动权限尚不能可信校验的限制，并直接在 Guest 内试用：已将目标 NX809J/API 36/ARM64/N64 的标准振动桥默认启用，保留 Recovery、缺少宿主振动器或受管 Binder 等情况下的条件与回退；原厂 QTI 独立隔离实验仍关闭，仍只调用宿主 Android 振动 API。标准单元测试 541 项、失败 0、错误 0、跳过 54；standard/clone Debug 构建成功，已覆盖安装 clone 且保留现有 Guest。安装后的现有 Guest 40 秒报告启动完成，标准振动服务可发现并已进入桌面；普通应用或交互触发的效果待实机验证，新实例首次启动未实测。已知超出能力的某些合法请求和单次超过 5 秒可能导致桥断开，后续可检查丰富效果或原厂 QTI/RichTap。

- 用户在已安装的 clone Guest 内自行触发振动并确认手机有振动，具体界面与操作未记录；试用后标准振动服务仍可发现。此结果不扩大至未测的其他应用、复杂效果或新实例。

### 0.3.0 计时器触发后振动桥退出（2026-11-09）

- 回归记录显示来电振动后计时器和后续来电不再振动，关机时出现 vibration 错误。受控复现中，来电期间宿主记录周期振动；设置计时器时临时脱敏诊断捕获目标振动接口 `perform` 事务 4，桥将其判为未支持并关闭连接，关机时显示 `Invalid vibration Binder event`。重试关机后 Guest 进程退出。捕获的是事务元数据，不含请求内容或用户身份；不能由此推断其他所有断桥原因。
- 为固定长度、空回调的 `perform` 请求返回标准“不支持”响应并保持连接，不伪报效果成功；其他未验证形式及文件描述符继续拒绝。新增同连接 `perform` 后普通振动、重复振动和畸形请求回归，WSL 普通及 ASan/UBSan 测试通过。标准单元测试任务成功（结果 541 项、失败 0、错误 0、跳过 54，任务复用已有结果），standard/clone Debug 构建成功。
- 修复后的临时诊断版在现有 clone 中重现来电、设置和运行 5 秒计时器：此前触发断桥的操作后线程仍存活，倒计时到时宿主记录连续 500 毫秒振动，Guest 正常关闭且未再显示上述错误。再次来电已触发，但未单独确认其实际振动；实机体感和更多场景仍需复核。无诊断版 clone 已覆盖安装，保留 Guest 数据，运行库 SHA-256 `f2ecf1267d33dd98cf756d8cce45341210924bde60e242ca7dc737c673884054`，APK SHA-256 `df7ad4a88f0ede539aeff46ea5d7e00941654cbddeb083c2d8ae89aa8d3b2ea9`；安装后已启动，桥线程存在，最终版完整连续流程尚未重测。
- 回滚：正常停止 Guest 后，核对 `%TEMP%/rme-vibration-diag-qypoxffb/original-library.so` SHA-256 与此前资产一致，再恢复该库及本次振动源码和测试、重新构建 APK；原安装包备份位于同目录 `original.apk`。测试来电记录保留在 Guest；未修改原始固件。其他合法未支持请求、超过 5 秒请求及 Guest 身份权限限制仍未解决。

### 0.3.0 阶段验收确认（2026-11-09）

- 实机回归已覆盖真实普通应用、远程网页及更多音视频场景，最终安装版的连续来电和计时器振动复测通过；具体应用、网页和格式未逐项记录。断连恢复及长期运行稳定性暂不作为本阶段阻塞项。按此范围，0.3.0 基本功能阶段验收通过。

### 0.4.0 息屏主题人物分割调查（2026-11-09，未修复）

- 切换主题并切回后，息屏画面截到人物胸口。同期 Guest 增量日志两次记录原厂 QNN HTP 设备初始化失败、跳过分割并返回空前景，随后生成息屏缓存。对 `writeStaticImage` 的 DEX 跳转地址再次独立核对：有前景时保留根据最高点计算的位移，无前景时才使用镜面壁纸偏移。传输初始化失败的底层原因未确定。
- 固件另一套 TFLite 人物分割模型已在电脑 CPU 上对原厂主题图运行；候选预处理得到的人物遮罩存在误检和缺口。按从原厂 QNN 库静态分析出的最大连通域与填洞规则处理后，误检减少但仍有明显缺漏；完整预处理、算法等价性和原厂效果均未验证。主题包未发现可直接复用的独立前景图。
- 运行中原厂 SystemUI 的 `libzte_saliency.so` 直接从签名 APK 映射，现有引擎未提供针对该库和进程的加载重定向。上述模型未接入产品；未修改原厂 APK、固件或设备数据，也未构建、安装或验证修复。兼容矩阵保留故障状态。
- 继续验证软件后备：第三方发布的 MIT BiRefNet Lite 512 ONNX 在电脑以原厂主题图推理，人物大体完整但误选部分背景；按原厂后处理近似生成 BGRA 与最高点的离线原型覆盖空图、无效输入、重试和释放，未验证与原厂输出等价。独立 Android ARM64 宿主进程以合成输入测试模型，加载 5.465 秒、推理 2.441 秒、最大 RSS 730968 KiB，测试文件已清理；模型和运行库未加入项目。
- 自造 APK 在目标固件 Bionic 和 QEMU 下验证定向加载拦截、另一库不变及 APK 摘要不变。进一步隔离 ART 测试起初因测试环境的重复 libart 映射而崩溃；恢复原厂 Bionic 符号链接和链接器配置后，自造 Java `System.loadLibrary` 测试基线为 11/33、拦截后为 22/33，均退出 0，APK 摘要不变。这是 QEMU 中独立测试进程，不验证真实 SystemUI 命名空间、zygote 继承或手机。没有接入兼容库或更改设备；此问题仍未修复。
- 后续电脑实验新增未接线的 `native/gueststubs/aod*` 源码：软件模型、遮罩和 JNI 的独立测试通过，真实模型处理该主题图仍会误选部分背景；隔离 ART 已以自造输入运行真实模型。加载实验发现额外失败加载会改变 `dlerror`、工作线程方案可在嵌套加载时死锁、简单缓存句柄可在卸载后误替换，`RegisterNatives` 失败也可能只注册部分方法。上述实验不得用于正常启动；当前尚无经过验证的安全接线、产品构建或修复后验收。
- 后续方向调整为优先还原原厂动画，静态缓存替换已停止；原竖屏缓存摘要仍为 `42d64873093a88364cbb6ec9b562bcf017b93f874b9635e8b8932799c97c5855`。停止前曾在同目录建立一份内容相同的 `.rme-backup-a80805d5b4f84958aa4cffbb50265e12` 备份，因安全标签与原文件不一致未用于替换，暂保留；没有改动原始缓存或安全策略。
- 2026-11-10 在已确认的高通 QAIRT 许可范围内，在电脑使用官方 2.37.0 HTP 参考后端对目标主题计算一次：模型执行退出 0，输出 640×640 有限 FLOAT32，经按固件逻辑的离线后处理得到人物前景，最高点 `(880,166)`；结果、输入及清单保存在 Git 忽略的 `local-data/aod-oem-20261110/`。原厂工具元数据报告该 context 的 HTP 架构为 V79、SoC 69；此前从字符串判断为 V75 不可靠，已更正。模型结果是在电脑处理原厂主题 JPEG 得出，Guest 的解码及裁切像素尚未对照。
- 按固件布局和定位公式另生成了三种电脑近似预览，人物头部位置与主题预览接近；它们使用 Pillow 而非 Android Canvas，尚非实测原厂缓存。原厂组件仍需同步读取前景状态、最高点和三个缓存；安全导入、切换主题后的失效及回滚未实现。本轮设备离线，没有安装或实机运行该结果；本模块仍未修复。
- 设备重新连接且 Root 可用后，核对现有 clone 实例 `9ewdvb`、Guest 1216×2688/520dpi 和当前主题构图；背景 JPEG 经重编码，与电脑原图并非逐字相同。原设置 `one_shot_main_top_y` 不存在、`one_shot_has_main_image=0`；通过 Guest SettingsProvider 依次写入 166 和 1 并读回。正常关闭 Guest 并核对相关进程退出后，在运行副本 SystemUI 私有 files 目录备份三张原缓存，导入原厂模型前景和电脑近似三图；背景未修改。四文件经 SHA-256、属主、权限、SELinux 标签和 `user.aemu.own` 检查，替换是逐文件 rename 而非整体原子操作，仍有检查与写入之间的并发竞态。
- 重新启动同一 Guest 后 `sys.boot_completed=1`，设置和四文件摘要保留，SystemUI 和 statusbar 可发现；触发息屏后约 23 秒出现 AOD，人物头部伸出圆形，截图确认效果。一次 Guest 电源键唤醒回到锁屏，间隔截图显示画面变化；宿主录屏只取得一帧。没有切换主题测试，也未将预计算缓存接入应用自动生成或分发。原始固件未改，按此前验收范围仍不能称原厂 QNN 服务、其他主题或自动重建已修复。具体重建和条件回滚见 [AOD 缓存记录](AOD_THEME_CACHE.md)。
- 实机确认动画可观看且表现正常；这不扩大到其他样式、主题或切换主题后的缓存重建。
- 2026-11-10 清除旧实例后，正式版导入带 ztecfg 的新实例 `3pafyq`；首次启动确认 `IP_MC_W_CN`、`default_theme_64`，再次记录 QNN 初始化失败和空前景，截图复现胸口裁切。按上次受控方式写入设置、备份并替换指定运行副本缓存后重启，设置和摘要保留，清晰截图确认头部位置恢复；此次动画未单独确认。事务与条件回滚见 [AOD 缓存记录](AOD_THEME_CACHE.md)。这是手动实例处理，尚非 EMU 自动修复。
- 新实例中壁纸管理器 PNG 经 Guest Android 解码得到的 RGB 摘要与原厂主题源图一致；该结果未直接捕获 JNI 实际输入。独立 Guest 合成测试验证手动配置的 ClassLoader 可优先加载目录库，并可使用 APK 内完整路径加载合成原库；实际 SystemUI 的 `nativeLibraryDir` 已查实，但真实 ClassLoader、原厂库命名空间和委托尚未实测。新增 `native/gueststubs/aod-fixed/` 为未接线的固定遮罩 JNI 原型：模拟委托、错误/异常、遮罩文件及全尺寸离线像素测试通过。原厂库身份、重入、路径祖先安全、首次加载失败语义及主题切换仍待核实；原型不得默认启用。
- 引擎对 Guest 不存在的部分 `/sys`、`/proc` 路径会回退宿主，故未在手机上进行原厂库的独立加载实验。电脑隔离测试曾用空 `/proc`、`/sys`、`/dev` 启动目标 ART 并加载合成主库，但额外合成依赖对照未通过；后续更严格隔离尝试分别因额外 FD、路由检查误判及 chroot 内 Python 布局模块缺失而在加载原厂库前停止。未加载原厂库，临时根已清理。
- 2026-11-10 后续完成固定主题的 EMU 导入接线：新实例暂存期间校验目标指纹、ztecfg 变体、SystemUI APK、原厂模型、代理和单通道遮罩；仅在目标条件满足时准备运行副本。代理保留原厂可用时的调用，对指定 RGB 输入使用离线计算的遮罩，不把模型或固件文件加入 Git。Windows standard Debug 测试 608 项、失败和错误均为 0、跳过 87；另在 Linux 文件系统执行 20 项导入准备测试均通过，原生代理合成回归及全尺寸离线像素测试通过。完整源码重建的代理摘要与打包资产一致。
- 卸载旧正式版后，安装 `0.4.0-beta` / code 8 并导入带 ztecfg 固件；初版导入按分析器报告的 KK 引擎选择路径。取消导入后，改用实际启动的 N64 引擎完成重装和导入。最终新实例 `uxbbdn` 首次启动时，真实 SystemUI 映射了代理、APK 内的原厂 JNI 及 `libzcv.so`，自动写入人物前景、三张缓存和最高点 166，未人工改动 AOD 文件或设置。截图确认头部位置正确、动画正常，切换其他主题再切回后仍正常；只读核对切回后背景、前景与缓存仍为指定结果。该设备只安装一个 RMEMU。其他壁纸、宿主、长期使用和原厂 QNN HTP 不因此视为已验证或已实现；运行副本和回滚见 [AOD 缓存记录](AOD_THEME_CACHE.md)。

### 0.4.0 虚拟 LTE 信号强度（2026-11-10）

- 在 `codex/0.4.0-1b-lte-signal` 阶段分支，沿用单卡共享基带状态补充 RadioNetwork 信号查询和主动通知：连接且 Radio ON 时返回固定合成 LTE 样本，由 Android 框架计算等级；关闭或断连时信号不可用，无模型保留原有 `REQUEST_NOT_SUPPORTED` 合同。其他未模拟制式标记为不可用。支持的 indication filter 和 LTE 上报条件保存并应用；回调更换、死亡及关闭处理和阻塞回调边界有定向测试。信号不读取宿主真实蜂窝状态。
- 先新增查询测试并观察到 ON 状态查询失败；最终离线 Radio 回归 `OFFLINE_RADIO_OK dex_classes=476`，覆盖查询、关闭、恢复、断连、通知、过滤与条件、回调替换和死亡，以及阻塞或失败回调对电源请求的影响。Guest-only Binder 测试仅编译，未在 Guest 内执行；故意注入的回调失败产生预期诊断。
- 使用 JDK 21 和现有 SDK 重建 `radio-service.jar`，标准及 clone Debug APK 构建成功，标准单元测试任务为 Gradle 缓存命中；两个 APK 的资源索引及 JAR 内容与新资产一致。JAR SHA-256 为 `6b6b9dad331ff5d034d506b8bacf907bf63bbf0865d09843b9d61314f43f4776`。覆盖安装标准 Debug 后启动已有 Guest，运行副本 JAR 摘要一致。
- 实机验收确认状态栏出现信号格，飞行模式往返后信号恢复；随后 Guest 电话框架报告 LTE RSRP -95 dBm、RSRQ -10 dB、RSSNR 10 dB、框架 level 4，其他未模拟制式为不可用。未采集飞行模式开启期间数值，未在本轮重新验收短信和来电、断连恢复或长期稳定性。该信号由 EMU 模拟生成。
- 原有未提交 JAR 在本机 `%TEMP%\redmagic-radio-before-lte-signal.jar` 留有备份。回滚须仅撤销本轮信号相关源码、测试及 JAR 变更，重建并覆盖安装；运行副本由资源准备流程更新。不要重置其他未提交改动或删除实例数据。

### 0.4.0 开放虚拟 Wi-Fi（2026-11-10，未完成）

- 虚拟 Wi-Fi 在 LTE 阶段分支的基础上开发。实机测试使用单一标准版应用和既有的 NX809J 原厂 Android 16/API 36/ARM64 实例。
- 本轮使用内部开放热点，默认 SSID `RedMagicEMU`，无需密码，不扫描宿主物理 Wi-Fi。已按需参考 AEmulator-Plus `v0.0.0.3.10`，实际基线 commit `d470c4db72c988ebfd7e16c6598dd1b48595566c`；客体源码、上游许可证和通知位于 `native/gueststubs/wifi/`。已增加 SSID、信号设置及侧边栏 Ethernet/Wi-Fi 选择；选择按实例保存，下一次启动生效。侧边栏选择及保存曾在标准版设备上实测；Wi-Fi 尚未启用，默认 Ethernet。
- 标准源码中 `TargetWifiArtifacts.RUNTIME_READY` 和 `WifiStub.SERVICE_IMPLEMENTATION_READY` 均为 false。两份 guest JAR 和 APK 已做过构建，但后续源码有修改，不能假定现有资产与源码一致；重新构建时须核对。设备当前标准版为保留 Ethernet 的构建，不能用它声称 Wi-Fi 可用。
- 实机临时实验曾在 `eth0` 上注册 Wi-Fi NetworkAgent，并由 Guest 完成一次 HTTPS 请求（HTTP 200）；未完成普通应用权限、设置页、DNS 独立查询、开关重连、宿主断网恢复和长期稳定性验收。服务退出后曾恢复 Ethernet，但系统原 `wifi` Binder 未自动恢复；正常重启 Guest 后原服务恢复。不要将 Ethernet 降级当作 Wi-Fi 验收。
- 当时的网络控制代码已对接口状态暂时未知的情况设置等待上限。`target_net_control_test.py` 曾稳定失败，断言客户端死亡后原 Wi-Fi Binder 未恢复，恢复实现处于未完成状态。目标 Guest 另实测普通进程降权后可恢复虚拟 UID 0，独立 Binder 服务收到该 UID；源码已有未装机的最小 UID 变更，但未解决消息头身份认证，也未验证启动、LTE、短信及来电回归。
- 当前方案接受仅可信客体应用的使用风险，权限与服务恢复尚未完成。
- 本轮后续修复了热点设置可能被启动统计旧快照覆盖的问题，并测试了开放热点十六进制 SSID 匹配；这些后续修改尚未安装设备。设备曾覆盖安装过标准调试版，侧边栏菜单可见、模式选择可保存；测试结束已选回 Ethernet。原厂 ROM 和原实例未删除。回滚应仅针对本阶段改动，先核对未提交差异和资产归属，不要使用全仓重置。
- 非 Binder 网络生命周期小修：模拟旧 NetworkAgent 的移除通知排队后断开并重连，修复前离线测试复现旧通知误断新连接；现对通知来源与当前网络进行比对，并在同一状态锁内检查和断开，监听器在锁外通知。模拟框架回归通过，也验证当前网络的通知仍可断开连接。通知发出前若另一个线程重连，服务层状态通知的顺序仍未验证；不据此启用 Wi-Fi。未重建资产、安装或实机测试；回滚仅撤销 `WifiGuestNetwork.java` 中本次回调来源检查和 `network_agent_test.py` 对应回归，保留既有改动。
- 非 Binder 替代路径调查：API 36 使用用户态运行引擎，通用 netlink 当前返回 family 不存在；现有 `WifiStub.kt` 仅用于 API 5–13。只读检查设备已有 Guest 运行副本的原厂 init 配置和程序字符串，确认包含 Wi-Fi HAL、`wpa_supplicant`、`wificond` 与 Qualcomm Wi-Fi 库，后三者均含 nl80211 相关字符串；字符串只能证明程序包含相关实现。可继续研究在 Guest 用户态模拟无线控制接口并保留原厂服务，但命令集、接口生命周期和数据通道均未确定，尚无可用实现。AOSP 的 [virt_wifi](https://android.googlesource.com/kernel/common/+/1cca65ac6abc0751e6bd0dfdd9564925e2a7ff07/drivers/net/wireless/virt_wifi.c) 和 [Cuttlefish](https://android.googlesource.com/device/google/cuttlefish/+/master/shared/BoardConfig.mk) 方案依赖内核支持，不能直接套用到当前共享宿主内核的运行模型。本轮未更改 Guest 或宿主、未启动无线服务、未安装或实机联网验证。
- AEPlus `d470c4d` 对照：其 [Wi-Fi 实现](https://github.com/somerandomusernam2/AEmulator-Plus/blob/d470c4db72c988ebfd7e16c6598dd1b48595566c/app/src/main/java/app/aemu/core/WifiEmu.kt) 使用现有网络通道，另注册 Wi-Fi 网络身份并由模拟服务提供连接信息；并非模拟无线驱动。[上游代码](https://github.com/somerandomusernam2/AEmulator-Plus/blob/d470c4db72c988ebfd7e16c6598dd1b48595566c/app/guest-wifi/java/app/aemu/stub/WifiStub.java) 对现代 Guest 接管系统 Wi-Fi 服务，注册 Wi-Fi 网络失败则可回退以太网；这一方案的回退路径为以太网。项目中已有对该网络身份路径的适配，前述临时 HTTPS 试验验证过一次数据连通；本轮不照搬服务接管或修改 Binder。上游 API 范围是代码声明，不证明 NX809J 固件实测兼容；本次仅源码对照，无构建或设备验证。
- Binder 修复聚焦于连接回调与服务状态的并发顺序。双线程测试复现旧断开通知覆盖重连。修复后，网络回调和服务状态转换共用转换锁，在状态锁内确认网络并断开，释放状态锁后才通知服务。调用者身份、权限校验和服务恢复沿用原实现。模拟测试覆盖旧通知、正常断开及移除失败时的 UNKNOWN。隔离原型 JAR 已编译，真实服务接线和失败清理时序待验证。正式资产和两道门控沿用当时的配置。回滚仅撤销共享转换锁接线及对应测试。
- 后续核实：首次网络注册发生于 Guest 主线程进入事件循环之前，网络回调正常经 Handler 排队，不能仅凭首次注册未持转换锁就断定发生并发故障。增加模拟框架测试，在注册期间产生回调并于注册完成后执行，验证当前网络被断开、通知恰好一次；离线测试通过。这不验证真实服务接线或失败清理，也不是设备运行结果；本次未修改运行代码或设备。
- 回调投递失败排查：离线试验曾观察到旧网络回调在重连后投递失败会报告故障；经复核，新旧网络代理使用同一个 Looper，`Handler.post()` 返回 false 通常意味着共享队列正在退出，不能因为回调来自旧网络便忽略故障。已撤回忽略该故障的试验修改，增加模拟测试确认该场景仍报告失败，并为并发测试添加超时保护。本次没有修改运行代码、安装或启动服务。
- 原型资产核查：使用 JDK 21 和 API 36 SDK 将当前 Wi-Fi 服务源码在隔离目录重复构建两次，均为 64 个 DEX 类，SHA-256 均为 `a0e35d44d4f1094ff375abc434cdf9f002aced06a7f2f16d0970b10552ee0604`；网络模拟回归和构建脚本两项单测通过。既有未提交资产的摘要为 `f1c445efa8ca7621491b47c80c10837d009312c7c2dee8976feb8103e53b1b62`，与当前源码构建不同；因为原型尚未通过服务接线、权限及恢复验收，未覆盖该资产，未安装或启动。两次构建结果位于本机临时目录 `rme-wifi-rebuild-c2f45dde530e4485b5c27918adb9c4bc`。
- Guest 独立状态探针在 WLAN 服务保持可用时运行。本轮未进行恢复或服务注册，仅在设备现有 Guest 中运行临时独立诊断程序，构造未注册的 `WifiService` 并直接调用本地状态方法。模拟先报告失败、后执行已排队的网络移除通知，修复前在目标 Guest 报 `queued network callback cleared a prior failure`；修复后仍保持状态未知，完整诊断输出 `WIFI_GUEST_RUNTIME_SMOKE_OK`。实现只删除 `onNetworkUnwanted()` 对先前 UNKNOWN 的无条件清除；正常事件仍设置断开，UNKNOWN 保留原有状态。原型 JAR 离线构建通过（64 类，SHA-256 `84b18919dababaf63c1eabc2626c16d510bd5262184965aab02d709c22c7fc27`），网络模拟和构建脚本测试通过。开始时直接运行 `app_process` 的探针未及时退出并显示 `Killed`，改用项目的 Guest Java 命令包装器后才取得明确测试结果；不将前者当作产品故障。只验证方法顺序，不验证真实 Looper 退出、广播、网络接线、身份或恢复；未安装 APK、替换产品资产或改变网络，相关设备临时文件已清理。回滚仅撤销本次 `WifiService.java` 相关一行及 `WifiGuestRuntimeSmoke.java` 对应探针，保留其他未提交改动。
- 2026-11-10 本轮虚拟热点范围为开放连接，不要求额外登录认证；保留现有安全检查。设备通过 Root 安装标准版并操作，不安装分身。ADB 已连接并确认 Root 可用；未安装 APK、启动 Guest 或更改设备数据。
- 本轮重新执行 `target_net_control_test.py`，仍失败于 `dead client did not restore Wi-Fi Binder`：客户端死亡后虽恢复 Ethernet，原 Wi-Fi 服务未恢复。恢复原 Binder 的试验涉及非原子检查与发布，可能覆盖并发注册的服务，已撤回试验改动；两道启用门控保持关闭。宿主网络变化接线和真实 ROM 的连接、权限、回调及恢复待设备检查。
- 使用 JDK 21 在隔离目录重新构建当前 Wi-Fi 原型服务与网络 JAR，分别为 64 和 11 个 DEX 类，SHA-256 分别为 `84b18919dababaf63c1eabc2626c16d510bd5262184965aab02d709c22c7fc27`、`a37f1b3098c3c9fb65bbeaf9ee9913163cdbe8b3be50c98c8d21c68fc37179ad`；没有替换现有打包资产。网络代理模拟测试和 Wi-Fi 构建脚本 4 项测试通过。指定 JDK 21 执行标准 Debug 单元测试及 APK 构建成功，测试报告 633 项、失败和错误均为 0、跳过 90；APK 仍禁用 Wi-Fi 原型，不可作为 Wi-Fi 验收包。
- 本轮继续处理服务恢复原型：`TargetNetControl` 在交接时保存原及替代 Wi-Fi Binder，死亡时先尝试恢复原服务，再等待接口空闲并恢复 Ethernet；显式回滚和死亡恢复均检查网络是否实际出现在 ConnectivityManager，服务未确认、网络未确认、等待超时或异常时标记 UNKNOWN。`WifiStub` 传递交接 Binder 并在回滚前检查服务归属。对应模拟测试先复现原 Binder 未恢复、误报 ACTIVE、接口忙阻塞、服务在恢复间变化及网络未出现在系统列表，再通过；未验证真实 Guest Binder 调度。
- 这仍是未启用的尽力恢复：ServiceManager 查询与发布非原子，独立模拟已复现检查后其他服务注册可能被覆盖；已缓存旧 Binder 的客户端也不会自动切换。`TargetWifiArtifacts.RUNTIME_READY` 和 `WifiStub.SERVICE_IMPLEMENTATION_READY` 仍为 false；不得把本轮结果当作 Wi-Fi 验收。代码回滚须仅撤销本轮 `TargetNetControl.java`、`NetStub.java`、`WifiStub.java` 和 `target_net_control_test.py` 中恢复相关改动，保留此前未提交内容。
- 本轮通过无线 ADB 使用标准版启动既有实例，Guest `sys.boot_completed=1`、原 Wi-Fi 服务存在且处于关闭状态，系统网络为 Ethernet；未安装 APK、未改手机联网设置，未测试开放热点、DNS/HTTPS 或重连。新原型 JAR 隔离构建通过（Wi-Fi 服务 SHA-256 `1a2f2bca1ff6f00cf436c119740627b255659b8bb4b1be7bcc08d20abf52f085`，网络服务 SHA-256 `4ced6716ab59a77d1da3d24603849494a3c94a6bb1634a4fa3a20b076829263c`）；未替换打包资产。标准 Debug 构建成功、测试任务为 UP-TO-DATE，生成的 APK 未包含本轮新原型代码。
- AEPlus 路径实机隔离实验优先采用其简化思路。未更改正式源码、APK、原厂固件归档或宿主物理网络；在现有 Guest 中确认旧 NetStub 的 PID、UID、参数和实例归属后，临时切换到独立构建的网络控制与 Wi-Fi 服务。系统 Context 的 WifiManager 报 `enabled=true`、SSID `RedMagicEMU`、networkId 0；ConnectivityManager 观察到 TRANSPORT_WIFI 且未验证为 VALIDATED；DNS 解析 www.baidu.com 成功，通过该 Network 显式绑定 HTTPS 返回 200。另一个独立 Binder 探针反射目标 IWifiManager 的 225 项事务，输出 `WIFI_GUEST_BINDER_SMOKE_OK`；使用本地 Parcel 和部分模拟 Context，不能代表普通应用或设置页通过。
- 同一实验中，系统 Context 的 `setWifiEnabled(false)` 抛出与获取 settings provider 时 caller 无法识别有关的 SecurityException，关闭未成功，重连未执行；仅有异常摘要，未保存能确定具体源码行的堆栈。随后从标准版菜单重启既有 Guest，确认 boot_completed=1、原 wifi 服务和 Ethernet 恢复、临时控制服务及 Wi-Fi 进程退出；设备临时资源已清理。本轮 Wi-Fi 只曾临时运行，正式 APK 仍未启用，宿主断网恢复及普通 App 均未验证。
- 后续定位并修复开关的 Settings provider 访问失败：目标 Guest 独立探针确认普通 ContentResolver 方式因独立服务进程未被 AMS 当作应用线程而抛 SecurityException；外部 provider 接口可读取同一 `satellite_mode_enabled` 并释放引用。新增 `WifiSettingsReader`，保留调用权限及用户限制检查；读取失败、响应缺失或类型错误不伪装为关闭。离线失败回归与构建脚本测试通过，目标 Guest 直接验证读取值为 0。源码回滚仅撤销本轮 `WifiSettingsReader.java`、`WifiService.java` 中对应调用、`build_service.py` 和相关测试的本轮改动。
- 再次使用独立实验资源受控接线：目标 Guest 系统 Context 中，关闭 Wi-Fi 返回 true、WIFI 网络消失；重新打开返回 true、WIFI 网络恢复，SSID 为 `RedMagicEMU`，再次 DNS 解析成功、绑定 Wi-Fi 的 HTTPS 返回 200。普通权限 Guest 测试包已临时安装，但显式广播未返回探针结果，故普通应用验收未通过；测试包已卸载并确认不存在。实验结束重启原 Guest，确认 boot_completed、原 wifi 服务及 Ethernet 网络恢复，实验控制服务不存在。设置页、宿主断网恢复和异常退出后的完整恢复待设备检查。
- 本轮标准 Debug 构建与测试任务为 UP-TO-DATE；正式资源仍为旧 JAR，运行门控仍关闭，APK 未包含本轮 Wi-Fi 修复。不得把临时实验或已构建的原型当作已安装功能。
- 本轮先定位普通应用探针无结果：目标 ROM 的 autolaunch policy 跳过了无前台入口的后台广播；添加前台 Activity 后，默认 Ethernet 下探针能完成公开 API 读取。随后在同一 Guest 的隔离 Wi-Fi 接线中，普通应用 `ACCESS_WIFI_STATE`/`ACCESS_NETWORK_STATE` 已授权且位置未授权时报告 Wi-Fi 已开启、TRANSPORT_WIFI、SSID unknown、networkId=-1；授予该测试应用精确位置后报告 SSID `RedMagicEMU`、networkId=0、TRANSPORT_WIFI，VALIDATED=false。两次均由前台 Activity 直接调用并完成。
- 实机诊断发现 IWifiManager 的 `isScanAlwaysAvailable` 被现有守卫拒绝。先在目标 Guest 未注册服务的本地 Binder 测试复现授权检查失败，再补 API 36 的 ACCESS_WIFI_STATE 检查并返回 false（不支持 Wi-Fi 关闭时扫描）；修复后目标 Guest Binder 探针和离线守卫测试通过，隔离 JAR 已构建。回滚仅撤销本轮 `WifiServiceGuard.java`、`WifiService.java`、`WifiServiceGuardTest.java` 和 `WifiGuestBinderSmoke.java` 对该方法的修改，保留其他工作。
- 请求启动目标 ROM 的 Wi-Fi 设置页后，实际前台仍为测试应用，未看到设置页或热点，设置页待设备检查；临时诊断记录到部分未实现事务，但未识别调用者，不能推断它们是页面未打开的原因。完成后卸载唯一普通探针并重启原 Guest，确认 boot_completed=1、原 wifi 和 Ethernet 恢复、实验控制服务与临时 Wi-Fi 进程不存在，设备临时资源已清理。正式 APK 仍未启用虚拟 Wi-Fi，且打包 JAR 未更新；标准 Debug 构建和单元测试任务为 UP-TO-DATE。
- 继续设置页排查：此前截图为快捷设置 WLAN 浮层；在默认 Ethernet 下关闭浮层后，Guest 的 WifiSettingsActivity 可正常显示且 WLAN 关闭。Android 16 目标 Guest Binder 测试先分别复现 connect/forget 在 FLAG_ONEWAY、null reply 下被错误拒绝，随后修复并验证成功/失败监听器、权限拒绝和同步调用；UID 1000 Attribution 分支未实测。
- 临时 Wi-Fi 接线时，已有 WLAN 设置页仍显示关闭；重启 Guest 设置应用后，DropBox 显示 NPE 位于 WifiManager.getWifiConfigForMatchedNetworkSuggestionsSharedWithUser，WifiPickerTracker 栈调用。针对目标 ROM 的 shaded ParceledListSlice 参数及结果，先复现旧代码失败，再为 Settings 或 Setup Wizard 权限提供非 null 空列表，目标 Guest 未注册 Binder 测试覆盖权限、null/空/非空输入及尾随数据；离线回归和 66 类 Guest 原型构建通过。服务恢复离线测试通过；标准 Debug 构建与测试任务 UP-TO-DATE，正式 APK 仍未启用新 Wi-Fi 代码。
- 新版临时服务的设置页复测过程中手机前台切换到其他应用；为避免干扰，停止 UI 操作，Guest 仍运行本次临时 Wi-Fi/网络服务，未取得可确认的设置页结果。设备切回后需重启同一 Guest 并确认原 Wi-Fi、默认 Ethernet 恢复，再清理本轮 `rme-settings-exp-48f-` 唯一临时文件；不得宣称恢复已完成。
- 确认可切回后，已通过标准 EMU 菜单重启同一 Guest；确认 boot_completed=1、原 wifi 服务和 Ethernet CONNECTED 恢复、正式 NetStub 运行、临时 WifiStub 与控制服务退出。本轮 `rme-settings-exp-48f-` 唯一设备临时资源已逐项清理；设置页在新版服务下仍未完成复测，不可标为通过。
- 回归现象：WLAN 设置页信号缺失。原厂目标 Guest 的只读 `calculateSignalLevel` 测得 0..4 级、阈值为 -90/-85/-75/-65 dBm；对真实事务先复现原型不支持，再按目标阈值实现并验证边界和非法参数。连接信息的 primary 标记也在目标 Guest 独立探针先复现 false，再修复关联态为 true 并验证权限脱敏；真实设置页的最终信号图标及连接标记待设备检查。
- 使用隔离标准测试 APK 对 EMU 网络模式做了一次 Wi-Fi→Ethernet 往返：前次 Wi-Fi 模式可得到 TRANSPORT_WIFI、DNS 和绑定 HTTPS 200，Ethernet 模式恢复后为 ETHERNET CONNECTED。后一次含 signal/primary 的测试启动暴露 WifiStub 在控制服务发布前即查询并退出的竞态；现新增有界等待并保留超时/异常时原网络。本轮测试随后结束并暂停设备操作，因此最终等待器代码、最终 APK 均未执行测试或设备验收。
- 本轮暂停手机操作；停止前原标准 APK、Ethernet 偏好、配置和两份 Guest JAR 已按摘要及权限恢复，手机停在 Android 调试兼容性提示，Guest 最终启动和网络状态未验证。之后仅在电脑隔离副本重建 Guest JAR 和 standard Debug APK，构建命令返回成功；隔离测试包开启 Wi-Fi 实验门控，未安装、未测试，正式工作区资产和门控仍未更新。
- 调试端口恢复后，针对控制服务已发布但尚未 armed 和 Ethernet Network 尚未进入 ConnectivityService 的启动竞态，离线先复现失败，再加入同一 30 秒窗口的就绪检查：仅在控制 ACTIVE 且对应 Network 可见并确认 eth0/Ethernet 时交接，异常/超时保留原网络；有界等待/网络 readiness 回归与相关构建测试通过。该期限不能强制打断本身永久阻塞的同步 Binder 调用。隔离 standard APK 在同一标准包上覆盖测试；Wi-Fi 模式本次成功启动并使用 eth0，WifiInfo SSID RedMagicEMU、RSSI -45、IsPrimary=1、networkId=0，TRANSPORT_WIFI/CONNECTED 且 VALIDATED=false，DNS 和绑定该 Network 的 HTTPS 返回 200。
- 原厂 WLAN 页面仍把 RedMagicEMU 放在“选取网络”下，未标记已连接。AOSP Tracker 依赖已保存的 WifiConfiguration 与 WifiInfo networkId 匹配，原型拒绝 `getPrivilegedConfiguredNetworks` 是已观察到的缺口，但尚不能断定是目标页面唯一原因。已按 Android 16 的目标签名接入受限直接 Settings/SUW 查询，保持 READ_WIFI_CREDENTIAL、ACCESS_WIFI_STATE、AppOps 包归属与 Attribution 检查；已存网络返回开放配置 id=0，忘记后返回空列表，其他未审计调用/转发链保持拒绝。目标 Guest 本地 Parcel 红灯已复现，绿灯因 AttributionSource 要求真实 Binder 入站事务而无法完成；已准备独立诊断名的跨进程探针，尚未在设备执行。
- 本轮设备操作在该阶段结束，此后未再操作手机。最后确认手机运行的是同包名标准实验 APK，虚拟 Wi-Fi 测试现场保留。最新 privileged-config 修复仅在本地隔离 standard Debug APK 构建并核验，产物位于 `%TEMP%\rme-wifi-mode-apk-p43iibfs\guest-build\privconfig-bb9ca485b31542748b66fdcdce2c69c6\app-standard-debug-privconfig.apk`，未安装或实机验收；正式工作区资源及 readiness 门控仍未更新。原手机 APK 和运行副本备份仍保留于先前记录的临时目录。
- 设备恢复授权后，将上述隔离实验 APK 覆盖安装到同一标准包，确认签名、安装摘要、应用 UID 和既有实例；覆盖前另行备份当时的 APK、网络偏好和客体 Wi-Fi JAR。Guest 设置页的 RedMagicEMU 出现在当前网络，显示已连接和信号；WLAN 关闭再打开可自动重连。客体探针解析 DNS 成功，通过 Wi-Fi Network 访问 HTTPS 返回 200；Android 系统未标记 VALIDATED，设置仍提示无法访问互联网，未找到具体原因。探针发出完成标记后仍有进程存活。核对身份后结束该探针并清理输出。
- 正常重启 Guest 后复现快捷 WLAN 浮层显示关闭，但设置页已连接、Wi-Fi 服务和网络注册均已确认。仅重启客体 SystemUI，等待完全恢复且不切换 WLAN 后，浮层转为 RedMagicEMU 已连接；其 WifiTile 不再处于切换中。这支持旧界面进程持有过期状态或服务的假设，尚未直接读取其 Binder 引用，也未实现正常启动时自动同步。实验包和同一 Guest 仍留在手机运行，宿主物理 Wi-Fi 未改；正式资源和门控仍未更新。
- 离线测试先复现 Wi-Fi 初始关闭或未连接时交接失败；随后调整启动回调，使这两种状态下发布服务而不注册网络，原已连接路径和失败回退测试通过。客体服务已隔离构建，但此改动不在上述手机实验包内，未实机验证；开关及保存状态持久化尚未实现。
- 继续验证时，当前 Guest 的普通 DNS 查询成功，但 `android_res_nquery` 得到 `401\0` 而非 DNS 报文；已有采样也确认发往 Wi-Fi 的 raw DNS 请求进入了不支持 `resnsend` 的宿主代理。仅在隔离快照中实现该协议并构建实验标准包，原安装、偏好及客体 JAR 另行备份后覆盖同一包实测：普通及带 bypass 位的 raw DNS 均返回有效答复，Wi-Fi 由 Android 自行标记为 VALIDATED，设置页不再显示无法访问互联网。隔离实验仍有以下问题。该实验未正确映射失效或非当前 guest netId，宿主 API 26–28 不支持 rawQuery，bypass 未等同于绕过宿主 Private DNS，启动调用本身也未证明有界；因此该实验接线未保留在正式工作区。
- 该实验包冷启动后 WLAN 详情正常，但快捷面板仍显示关闭且状态栏无 Wi-Fi 图标。仅重启 Guest SystemUI、未切换 WLAN 后，快捷面板、状态栏和 VALIDATED 均正常。设备当前仍运行同一实验包（SHA-256 `abcb16c5481e9544e2fa5d7561891b2c94a9aeed75847e058c6737ae49db0729`）及原 Guest；此为临时现场。另确认引擎在宿主拒绝 SO_MARK 时返回成功，之前的 HTTPS 请求未核对 guest Network 的实际路由。未修改宿主物理 Wi-Fi。
- 根据目标 ROM 的 WifiManager DEX，新补其构造时调用的 `getVerboseLoggingLevel` 查询事务：只返回虚拟服务实际的关闭等级，并检查接口 token 和尾随数据。离线失败测试及修复后 dispatch/guard/构建测试通过；目标 Guest 真实 Parcel 的独立本地 Binder 冒烟返回 `WIFI_VERBOSE_LOGGING_CONTRACT_OK`，覆盖正确返回、错误 token、尾随参数及缺少 reply，未注册系统服务。当前手机实验包不含这一改动，且此项本身不解决 SystemUI 的旧服务引用。
- 本轮工作区以 JDK 21 运行 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 成功（633 项、失败和错误 0、跳过 90）；Wi-Fi dispatch、断连启动、控制就绪/等待、网络代理与构建脚本测试通过。工作区标准 APK 的 Wi-Fi 门控仍关闭、打包资产不是手机上的实验 DNS 实现，不能把此构建当成 Wi-Fi 功能验收。隔离快照另对 raw DNS 的启动阻塞和取消建立有界测试，尚未实机或接入产品。
- 后续隔离同包实验中，关闭客体 WLAN 后，状态文件记录 enabled=0/saved=1，Wi-Fi 网络消失；Guest 正常重启后仍为该状态，服务以未连接状态接管。随后在设置中重新打开后，状态为 enabled=1/saved=1，Wi-Fi CONNECTED/VALIDATED。另行记录忘记和重新连接通过；该结果未独立核实是否包含重启。系统界面仍可能显示旧状态。设备测试暂停后，一切手机和 Guest 操作已停止，后续仅做电脑离线工作。
- 本轮补充状态文件错误处理：Guest 网络变更成功后才提交文件与内存；保存异常会尝试恢复网络并核实磁盘，无法确定结果时标记未知。文件读取使用 NOFOLLOW 和长度上限，写入经同目录原子替换并同步目录；相应失败/并发测试通过，目录同步和 NOFOLLOW 机制的独立探针曾在目标 Guest 的 `/data/system` 临时目录通过。另修正服务发布与初始通知的竞态、连接信息查询与状态切换的并发，已通过离线复现及回归；这些后续修改尚未装机。
- 实机冷启动后，确认 Wi-Fi 已 CONNECTED/VALIDATED，单独发送准确初始状态广播仍不能刷新旧 SystemUI。后续方向调整为研究正常启动顺序：先准备虚拟服务，待网络确实就绪再报告连接，并保留原服务回退；早期原厂服务发布与 SystemUI 获取服务的时序窗口尚未在目标 ROM 测量，隔离模型不是修复验收。涉及底层 Binder 路由的一项隔离实验被自动安全检查中止，不继续该方案。
- 设备内验证忘记并重连后，本轮暂停设备操作；此后没有发送设备命令。后续需要连接设备时先核对同一标准包、实例、状态文件和备份，再进行针对性验证；不得将等待期间的离线结果记作实机通过。
- 设备离线期间，在唯一隔离快照合并 guest 状态保存/通知与 DNS 验证器原型，重建 guest 服务 JAR（SHA-256 `824e683c51cdf3bbd88a03fab0d9579aff69dd1788cba82018162d28c469e8de`）及标准 Debug 实验 APK（`%TEMP%\rme-wifi-integrated-offline-h338pi7x\source\app\build\outputs\apk\standard\debug\app-standard-debug.apk`，SHA-256 `a067985edf9f4ea18716a63dc5582e315a4dc68157a8c072d80d4605f8a26118`）。APK 包名/签名与先前标准包备份一致，两份 guest JAR 的资源索引、内容摘要核对一致；隔离的 DNS、网络模式及 Wi-Fi 配置等定向测试 54 项、0 失败、跳过 3。此包实验门控开启但尚未安装；其 DNS 验证器认证/异常恢复、宿主断网与切换、旧宿主兼容尚未实机验证，也不包含可用的启动顺序修复，不能当作正式版本。
- 对目标 ROM 离线反编译核对：SystemServer 同步启动原厂 WifiService，之后启动 Connectivity 和 SystemUI；目标 `service-wifi.jar`（SHA-256 `6180fd8563935d2ae35b9e889829116aa70687a7abe0b3674757a242957bb670`）的 `WifiService.onStart` 同步发布 `wifi`。该 APEX 中的直接发布调用仅此一处，但不能排除其他组件或反射再次发布，也不能推定时序间隔。另准备独立 Debug 时序诊断 APK（`%TEMP%\rme-wifi-timing-apk-189ea27fd1bc4c099a43dd08b6b651cf\source\app\build\outputs\apk\standard\debug\app-standard-debug.apk`，SHA-256 `a16c172f1f198d3771e1280e9b9d98bd94d8233232110d120d409883f9cf7f13`），包名/签名及 Wi-Fi、网络、诊断 JAR 的资源索引已核对；探针仅观察服务首次可见及同一 Guest SystemUI 进程首次出现的时间上界，身份不可确认时输出 unknown，180 秒采样截止不保证阻塞调用能中断。该诊断仅用于设备恢复后的受控验证，尚未安装；它含实验性 DNS/Wi-Fi 代码，并未修复启动顺序。早期服务与看护进程的隔离 Java 模型已覆盖部分失败/恢复，也明确复现非原子服务替换会覆盖并发注册者；目前未接入产品。
- 离线再次执行工作区 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 成功（635 项、失败和错误 0、跳过 90）；9 项 Wi-Fi 脚本及 API 36 SO_MARK 错误分支测试通过。工作区构建仍未启用 Wi-Fi 实验门控，不能把这些结果当作完整 Wi-Fi 验收。
- 设备恢复连接后，仅覆盖安装同一标准包的隔离诊断版本。目标 Guest 启动时记录原 Wi-Fi 首见约 4.9–6.1 秒、Connectivity 首见约 5.0–6.2 秒、SystemUI 进程首见上界约 6.3–7.5 秒；采样时间不是 SystemUI 获取 WifiManager 的确切时间。句柄对照的结果为 unknown。本轮范围不要求恢复原厂 Wi-Fi 服务，失败仍应明确并尽量保留 Ethernet。
- 提前发布准备态的隔离实验已在手机上运行：截图确认快捷面板 WLAN 开启并显示 RedMagicEMU；独立查询 Wi-Fi 状态为 ENABLED，但 networkId 为 -1、Wi-Fi Network 数量为 0，唯一 Ethernet 网络 CONNECTED/VALIDATED。此实验仅验证界面可见性，没有交接网络。随后在隔离目录准备两阶段交接包，故障与锁顺序模拟测试通过；实际交接和正式资源仍待验证。
- 两阶段隔离标准包 SHA-256 `4c2c862a51c45150a50ca5fef6944ab9bc5421620f1b4d24fcd3c808d4179398` 覆盖安装同一标准应用后，日志记录提前发布、会话确认和 Wi-Fi 网络交接；独立查询确认唯一 Wi-Fi 网络 CONNECTED/VALIDATED、Ethernet 数量为 0，Guest 普通 DNS、raw DNS 和 HTTPS 各通过一次。快捷面板显示 RedMagicEMU，状态栏出现 Wi-Fi 图标；快捷面板关闭后网络移除，再打开后自动重连，面板与状态栏恢复，重连网络 CONNECTED/VALIDATED。启动前一次广播记录 IllegalStateException，交接后广播是否成功未有明确日志；设置页开关、宿主断网恢复、切换网络及普通应用权限尚未完成本轮验证。该包使用隔离 DNS 原型。
- 为避免注册期间网络回调等待同一状态锁，在工作区先增加失败测试，复现网络确认阻塞回调；现启动准备期间不跨确认等待持锁，对中途取消和状态改变复核，相关状态转换、关闭启动及事务测试通过。该工作区修改尚未装入手机，正式 Wi-Fi 门控保持关闭。
- 隔离标准包 `6c25c9cbe3de9688d412e1c75d0101c3eb1c13454e236798993dfb377752fe64` 延后初始广播并加入 STOP 异常时的有界恢复。覆盖同一标准包后，目标 Guest 日志确认 boot 后广播发送、Wi-Fi 网络交接；唯一 Wi-Fi 网络 CONNECTED/VALIDATED，Ethernet 数量为 0，普通 DNS、raw DNS 和 HTTPS 各通过一次。状态栏 Wi-Fi 图标及快捷面板 RedMagicEMU 显示正常；异常注入、宿主断网恢复和此包的关闭重连尚未实机验证。
- 分步将提前发布/交接组件、状态广播及 `TargetNetControl` 的早期交接与有界异常恢复移入工作区。Guest JAR 独立构建及新旧控制路径模拟回归通过，正式资产与门控仍未更新；确认服务归属到停止网络及接口空闲到恢复之间仍非原子，结果不明时保留 UNKNOWN，不把 Ethernet 兜底记为 Wi-Fi 通过。
- 本轮范围不包含宿主断网恢复及指定网络绑定验收。普通 Guest 应用在先前两阶段实验包上实测：无精确位置权限时 SSID 为 unknown、networkId 为 -1；授予权限后为 RedMagicEMU、networkId 0，两次均检测到 TRANSPORT_WIFI 和 VALIDATED。探针已卸载；仅此探针的权限组合通过，不扩大至所有 API。
- 最终标准 Debug 包 `app/build/outputs/apk/standard/debug/app-standard-debug.apk`（SHA-256 `eacc0a62c6e001b37758805ecb8e64acb46bc13cc532dcb58ae57def57c33f6a`）已覆盖安装唯一标准应用。工作区启用目标 Wi-Fi，旧 Wi-Fi 资产备份在本机 `%TEMP%/rme-wifi-assets-before-final-20261111/`；APK 索引和 Guest 运行副本均核对 Wi-Fi JAR `eff12069c59994975e06b076b5d176f498ca27b6da1f0781d87c5ec4d7f1e58c`、网络 JAR `3213e361a966d1a3f81c87fd35b3546151d71f580e62feed8012bcd4b100e1bb`。目标 Guest 启动后 Wi-Fi 为唯一 CONNECTED/VALIDATED 网络，Ethernet 数量 0，状态栏及快捷面板显示正常；普通 DNS、raw DNS 与 HTTPS 各通过一次。最终源码 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 成功，698 项中 0 失败、0 错误、90 跳过；定向 Guest 状态、启动和交接测试通过。最终包未重做全部开关、忘记、短信来电和权限场景；先前实验与验收记录不能替代这些最终包回归。
- 最终包完成 Ethernet→Wi-Fi 模式往返，两种模式均正常使用；只读核对 Ethernet 模式唯一 Ethernet CONNECTED/VALIDATED，切回后保存 Wi-Fi 模式、唯一 Wi-Fi CONNECTED/VALIDATED、Ethernet 数量 0，状态栏显示 Wi-Fi。未更改宿主实体 Wi-Fi 开关。
- EMU 菜单已移除 Firmware apps 与 Camera 入口，对应功能代码保留。针对 Guest 飞行模式缺少 Wi-Fi 联动，增加设置状态观察和飞行模式前 Wi-Fi 开关状态的会话内记录：进入前开启则自动关闭、退出后恢复；进入前关闭则保持关闭。标准 Debug 包 `54cc213bb45cc6ddfc9b5e41f7dfb4b75c3787925c8732c0f0fdb171c134bd65` 已覆盖安装唯一标准应用；Guest 快捷面板实测两种初始状态的飞行模式往返，原先开启时退出后重新显示 RedMagicEMU。菜单两入口在上一验证包中已确认消失，最终包包含相同菜单改动。未改变宿主飞行模式和实体 Wi-Fi。
- 飞行模式前的恢复标记目前仅保留在 Wi-Fi 服务进程内：飞行模式期间重启 Guest 或 Wi-Fi 服务后，退出飞行模式不会恢复重启前的标记。观察器每秒读取 Guest 设置，首次读取只建立基线；极短时间内的飞行模式往返和启动早期变化不保证捕获。上述场景尚未实测，不得扩称通过。

### 实例网络、存储滑块和菜单文案（本地待实测）

- 在目标实例设置新增网络模式入口，与运行菜单共用同一选择弹窗和按实例保存的偏好；弹窗确认单独保存、取消不写入，下次启动 Guest 生效。Wi-Fi 选项的“实验中”文字已移除。此前手机上已确认设置入口和取消不改偏好，也确认保存以太网；设备测试暂停时偏好为以太网，恢复连接后需恢复原来的 Wi-Fi 并完成统一实测。
- 应用设置只保留关于和外观区域，移除新系统默认设置的显示入口，原有设置数据与功能保留。侧边栏使用到的 28 个菜单字符串已补齐至应用支持的 18 种语言；XML 解析、资源编译及键覆盖检查通过，未逐一实机切换语言。
- 内部存储滑块改为 64、128、256、512 GB、1 TB、2 TB；旧的自动或范围外值在打开设置时映射到最近可选项，仅在点击保存后写入。显示容量不分配空间，实际可用空间仍受宿主限制。`InstanceStorageOptionsTest` 覆盖上下界和旧值映射。
- 目标 Android 16 当前打包引擎的 statfs 分支未读取 `dev/.aemu-disk`，标准 APK 的容量滑块仅写入该配置。只在 `%TEMP%/rme-disk36-prototype-20261111/` 准备了隔离容量报告原型及测试：当前源码基线重建结果与标准引擎不同，且目标 ROM 存储页的系统分区叠加与舍入、statfs/fstatfs 实际调用和 `/storage/emulated` 路径尚需实机确认。实验引擎未写入正式资源；不要将 UI 滑块作为 36 引擎容量验收通过。
- 侧边栏中文显示已通过实机验收；实例入口与页面标题命名为“实例设置”，支持的语言已添加对应资源，应用设置标题保持原样。容量诊断 APK 正在同一标准包中受控测试，结果待记录。
- 电源菜单改用菜单图标，仅在实例网络模式为 Wi-Fi 时显示并与热点设置对齐；存储容量问题暂停处理。上述界面改动已在源码完成；隐藏的无效 Wi-Fi 草稿不会阻挡以太网模式下保存其他设置，且不会覆盖先前有效配置。基带名设为 `RedMagic Virtual Baseband`，`RadioModemService` 已在连接时返回该名称、断开时返回不可用，离线 Radio 回归通过；目标原厂设置可能读取属性或厂商特征，本地证据不足以确认该回复最终显示在页面上，尚未实机测试。新的 `radio-service.jar` 已打包，原资产备份于 `%TEMP%/rme-radio-before-baseband-20261111/`。该轮设备验收结束后未再操作设备。
- 截图确认基带版本已显示 `RedMagic Virtual Baseband`。随后调查存储：截图显示不论选择何档，关于手机的存储卡片均为 120 GB/512 GB；只读检查当时配置为 1 TiB，稳定版 Android 16 引擎未接入 `.aemu-disk`。目标 ROM 的 `MyDeviceInfoFragment.updateRomInfo()` 从 `StorageStatsManager.getTotalBytes` 和内部卷剩余量构造卡片，不是直接读取滑块。修订后的条件判断为：primary 不超过 512,000,000,000 字节时直接返回 primary，超过时查询 Vold；后者的舍入差不超过 3 GB 时返回舍入值，否则返回原值。先前 1 B 截图的准确来源仍未知。
- 离线以目标 ROM 的 `IVold.getStorageSize()J` 事务 78 和真实 `VoldStub` 建立失败测试，确认当前默认回复 0；仅在 `%TEMP%/rme-vold-size-isolate-20261111/` 准备了 API 36 读取实例配置的原型及测试。该原型不能解决 primary 不超过阈值时服务绕过 Vold 的分支，也未接入正式 JAR 或设备；旧隔离计算模型 `%TEMP%/rme-storage-model-20261111/` 的两个条件跳转方向有误，不可用于方案判断。设备暂停时实例设置表单仍打开，未关闭或修改其内容；本轮没有安装、更改实例设置或替换引擎。
- 为后续验证构建隔离 Guest JAR `%TEMP%/rme-vold-size-isolate-20261111/guest-build/redmagic-vold-service.jar`，SHA-256 `c870b0eb62ef666df73ad8960148ebbc48114e17945c5d4539f39f7bec14f0aa`；只包含 VoldStub 和所有权标记，离线事务分发回归通过。它没有接入启动流程或安装。

#### Guest 存储容量显示诊断（2026-11-11，未修复）

- 诊断对象为标准版 `app.redmagicemu` 中已有的 NX809J／Android 16／API 36／ARM64 实例。
- 现象：实例滑块可操作，但无论选择什么，“关于手机”存储卡片始终显示宿主 512 GB。曾出现 `1 B` 截图，具体成因未查实，不能将它归因于 `vold` 返回 0。后续截图确认基带版本显示 `RedMagic Virtual Baseband`。目前这不是容量显示通过。
- 真实定位：`GuestVm.kt` 在 Guest 启动时把 `diskMb` 写入运行副本 `/dev/.aemu-disk`；现有标准 API 36 引擎 `native/guest64/36/run64.c` 的 `statfs` 分支没有读取该文件。只读检查当前 `system_server` 和 VoldStub 使用的稳定引擎 SHA-256 为 `72767cb685adcb2f88ce29147fc4ec2feab79495e19dbecaa99561f3f93818ce`，当时文件是 1048576 MiB（1 TiB）；该设置后续可能更改，恢复工作前应重新核对，不假设当前值仍相同。
- 已定向反编译目标 ROM 首页 `com.zte.settings.deviceinfo.aboutphone.MyDeviceInfoFragment.updateRomInfo()`：它找内部卷，调用 `StorageStatsManager.getTotalBytes(fsUuid)` 和 `ZteStorageUtils.getVolumeFreeSpace`，再格式化为剩余／总量。更正：`StorageStatsService.getTotalBytes` 的内部卷分支在 primary 不超过 512,000,000,000 字节时直接返回 primary；超过时查询内部设备容量（可能命中缓存，否则读 `IVold.getStorageSize()`），仅当舍入差不超过 3,000,000,000 字节时返回舍入值。primary 的框架实现把 `/data`、`/system` 总量相加并按十进制档位取整。静态证据在 `%TEMP%/rme-disk36-prototype-20261111/StorageStatsService_decompile.txt` 与 `settings_classes13.dex.target.txt`；ROM 与未脱敏数据不得加入 Git。
- `VoldStub.java` 对目标 ROM `IVold.getStorageSize()J`（事务 78）走默认 0 回复。真实分发代码的 fake Binder 离线测试先复现失败，再在隔离副本验证读取容量配置：64/512/2048 GiB、无效／溢出／权限／缺失、token、尾随数据及旧 API／旧事务。原型源码与测试位于 `%TEMP%/rme-vold-size-isolate-20261111/`，独立 JAR SHA 见上一条。手机没有安装此 JAR；primary 不超过阈值时该方案被绕过，不是完整修复。现有 `aemu-stubs.jar` 多版本共用、包含其他服务，不能直接整体覆盖；若采用独立 JAR，须完成目标限定、所有权检查、安装缓存更新、原有 userstart/启动顺序和回滚验证。
- 另有隔离 `/data` statfs 试验：`%TEMP%/rme-disk36-prototype-20261111/source/` 的 `run64.c`、`disk_report.h` 和 `tests/disk_statfs_test.c`、`tests/disk_configured_test.py`；引擎 `%TEMP%/rme-disk36-prototype-20261111/disk-experiment.so` SHA-256 `b0d3a483c6cbae7761e7cddbbbbaa6a4bb3f1f0b4c69ab43461d791befee13e9`。曾在隔离诊断包下只读探针观察到选 512 GiB 时 `/data` statfs 返回 512 GiB，但 `fstatfs` 仍为宿主值、`/storage/emulated/0` 探针 EACCES；此结果不能外推稳定引擎或设置卡片。旧模型 `%TEMP%/rme-storage-model-20261111/` 的分支方向有误，其输出不可作为证据。不能为了显示正确全局篡改 `/system` 统计，也不应把虚拟报告描述成预留空间或强制配额。
- 现有标准引擎与当前源码同工具链重建摘要不同；当前 `run64.c` 有此前未发布 UID／SO_MARK 改动，直接替换会夹带这些变更。原 APK 或 ROM 不能用 Git 重置方式恢复；任何实验覆盖安装前应备份并核对包名、签名、唯一实例、配置和文件摘要，实验结束按条件恢复。最近本地标准 Debug APK `app/build/outputs/apk/standard/debug/app-standard-debug.apk` SHA-256 `211c58a84afb3c75b3ca031ca19d8148a3d11d5152ef52d2daad3fdb3d9ff6bb`。
- 下一步先保护手机可能未保存的“实例设置”表单；最近只读时它仍打开，未关闭或修改。之前只读界面检查留下 `/sdcard/rme-storage-trace-ui.xml`，如仍存在且确认属于本次，后续可单独清理。取得当前 system_server 实际 primary／vold／卡片统计和真实 statfs/fstatfs 调用证据后，选择只影响目标 Guest、不会误报真实剩余空间的可重建方案；先写失败测试，再隔离构建、核对资产并实机覆盖 64/128/256/512 GB、1/2 TB、切换重启、可用空间不超过宿主剩余和正常启动。不存在真实空间配额时要明确说明；不要把隔离测试或上次探针当最终验收。
- 设备恢复操作并连接无线调试后，使用标准应用的 BOOT 入口启动原有实例，已保存设置和运行副本容量均为 1048576 MiB。短时 Root strace 在 Guest system_server 中观察到 `/data` 的 statfs 及其重定向后的宿主调用；该次跟踪没有证明调用来自容量卡片，也不能用宿主调用结果代替 Guest 最终值。独立 Guest 容量探针启动时以退出码 137 结束，未取得 primary/StorageStats 数值；未找到能确认终止原因的日志。两份临时探针文件已按摘要核对并清理，随后确认路径不存在。跟踪结束后 system_server 和 VoldStub 进程仍存在；未验证页面、完整启动或容量显示。本次未更换引擎或安装 APK，修复仍需真实容量链路证据。
- 继续诊断确认上次 137 的直接原因：原探针未准备线程 Looper 就调用 `ActivityThread.systemMain()`，抛出未捕获的 RuntimeException，进程自行发送 SIGKILL；捕获异常和先调用 `Looper.prepareMainLooper()` 的对照分别验证异常及正常通过。修正探针 `%TEMP%/rme-storage-fixed-probe-20261111.py` 在现有标准引擎下运行成功：独立进程计算的 `/data`、`/system` 总量各约 240.54 GB，primary 为 512,000,000,000 字节；经 Guest Binder 查询的 `StorageStatsManager` 总量也为 512,000,000,000 字节，剩余约 120.27 GB（随时间变化）。Guest 原生 Binder 查询确认 Vold 事务 78 和 mount 事务 99 当时都为 0；这与 primary 在阈值时直接返回的正确分支相符。探针的 `/storage/emulated/0` 返回 Invalid path，不能外推页面访问权限；两份临时设备文件按摘要清理，后续独立检查路径不存在。目标 `services.jar`、`framework.jar` 与反编译来源摘要一致。
- DEX 复核确认两处实际跳转条件。`if-gtz` 在 primary 大于 512 GB 时跳至 Vold，在舍入差大于 3 GB 时跳至返回 Vold 原值。修正模型及五组边界测试位于 `%TEMP%/rme-storage-model-dex-corrected-20261111/storage_model_test.py`，仅为离线推演；旧模型及其结果无效。该模型结合现有 `/system` 统计表明，仅改 Vold 或仅改 `/data` 不能保证小容量档位，后续应调查只限定目标 Guest、可重建和回滚的更高层方案，并先建立失败测试；不得据此视为实机修复或真实配额。
- 虚拟容量需求取消，改为删除滑块并直接读取本机容量。`SettingsSheet` 已移除容量控件和打开表单时的旧值映射，保留已有 `diskMb` 元数据；标准 Debug 单元测试与构建通过。使用 JDK 21 临时覆盖本机失效的 Gradle Java 路径，没有修改 `gradle.properties`。安装前将标准 APK 备份至 `%TEMP%/rme-standard-before-storage-ui-20261111.apk`，核对 SHA-256 与新包签名；新构建与备份比较仅 `classes7.dex` 内容不同。Guest 关闭并重启后，已覆盖安装新包（SHA-256 `37ecb8a1cae699943a4c52aa4048c7579479f6d9d5ea59a05a094e35aca7fe45`），实例设置确认内存选项后不再出现容量滑块，打开表单后已有 `diskMb` 仍为 1048576 MiB；没有为测试保存该表单。恢复 APK 可使用上述备份覆盖安装。
- 当前手机实例所在宿主文件系统总量实测为 514,119,938,048 字节，Guest 路径式 `statfs` 总量受稳定引擎改写，当前服务报告的 512 GB 与真实总量并不相同。稳定引擎下，独立 Guest Java 探针通过 `Os.open("/data")` 和 `Os.fstatvfs()` 取得相同的宿主总块数和动态可用块数，临时设备文件已校验清理；此结果尚非实际容量服务修复。隔离 `services.jar` 方向验证先红后绿，候选仅改变目标方法两处跳转并返回 Vold 原值，六项离线测试和 DEX/ZIP 校验通过，原件保留在临时目录。该原型依赖 Vold 提供有效数据，不能直接安装；当前 system_server 同时映射原厂 services.odex/vdex，尚未证明它会执行补丁。不得关闭检查或移除原厂优化文件掩盖问题；下一步验证受控数据源、ART 实际加载和完整回滚，再构建安装并核验本机容量与界面。
- 重启现有实例后，`%TEMP%/rme-host-storage-live-red-20261111.py` 比较实例所在宿主文件系统和 Guest 容量服务，实测 514,119,938,048 与 512,000,000,000 字节不相等，按预期失败。隔离 Vold 原型 `%TEMP%/rme-vold-realfs-6e4a96f585e2/` 的事务分发先红后绿；独立 Guest 进程未注册服务，仅构造原型并本地调用 Binder 事务 78，返回的 514,119,938,048 字节与宿主一致，错误 token 和尾随参数被拒绝。单独的 ART 进程用受控 StorageManager 和 `InMemoryDexClassLoader` 对照原始、单跳和双跳 DEX，分别得到预期的 512 GB、64 GB 和 61 GB；没有构造或注册系统服务。对照使用独立测试进程。临时设备文件已按摘要清理。运行副本修改的所有权、独占、失败回退、ART 加载和回滚仍需验证，原型没有接入标准包。
- 移除滑块后，该范围已通过实机验收；继续接入真实容量读取不再是本次验收要求。上述真实文件系统容量与 Guest 返回值不一致的实测及原型局限仍然有效。


#### 加速度计桥接实现与验证（2026-11-12）

- 加速度计桥接针对 NX809J／原厂 Android 16／API 36／ARM64 的 N64 运行副本。实现、测试、两份 N64 HAL 资产的重建与回滚记录见 `native/sensorshal/N64_DIAGNOSTICS.md`。
- 实现现状：目标运行树准备时将原厂 `vendor/etc/sensors/hals.conf` 移至 `.aemu-parked`，避免其选择原厂 multihal；主机 `Sensors.kt`、`TargetAccelerometerProtocol.kt` 与 `native/sensorshal/n64_sensors_hal.c` 使用含 BOOTTIME 时间戳、精度和三轴的 AEM2 记录，包含会话状态栅栏。N64 ARM64 资产可用 `native/sensorshal/build_n64.py` 重建；回滚时须同时处理主客协议、两份资产及配置，不能只恢复一端。详细约束和已有离线测试命令见诊断文档；本次交接未重新运行构建或单元测试。
- 上一轮实机：安装的标准版 APK 与当时本机 standard Debug APK 摘要一致（`5c07841cba7db9e8d788d766ccd037ff5fcb0a61e8fde9efacb57c4a25ad4358`）；目标 Guest 启动后实际映射的 vendor HAL 摘要与工作区资产一致（`58ab4df04bf890465ab63ad50ada516afd967c10848c6393b68170ba79dbbcc4`）。`dumpsys sensorservice` 枚举一个三轴加速度计，最近 50 条事件时间戳递增；静止读数约为 (-0.14, 0.16, 9.84) m/s²。之后普通 Guest 前台应用 `app.redmagicemu.accelcheck` 显示正在接收及递增事件数，实际晃动后事件读数正常。这证明此次普通应用的基本数据响应，不证明所有机型或所有功能通过。
- 测试工具为临时 APK `%TEMP%/rme-accel-display-ydc1wf5b/accel-check.apk`，SHA-256 `75d6f98cd22d180112e297ccec05f4105a0220070951f19b84e738765efc839c`；无额外权限，前台显示 X/Y/Z 和事件数。已安装在 Guest，供实机测试；传输暂存已清理。若以后需移除，只在 Guest 内卸载 `app.redmagicemu.accelcheck`，不要卸载宿主标准版或删除实例。上次操作结束时 Guest 正在运行；本次只读 `adb devices` 已无连接，故当前手机和 Guest 状态未知，继续操作前重新核实，不要假设无线端口或前台状态仍有效。
- 后续可检查逐轴方向和量程精度、Guest 自动横竖屏、暂停恢复、flush、服务重启及多宿主机型。曾观察 Guest `WindowOrientationListener$AccelSensorJudge` 订阅加速度计；还受 Guest 自动旋转设置和应用方向约束影响。当前未接入陀螺仪等传感器，不应把加速度计当作游戏陀螺仪。宿主自然横屏、折叠屏及 OEM 方向配置须分别验证；不可保证所有手机。
- 曾按 Guest 关机请求停止并观察进程退出及状态文件非活动；停止时同时记录 `accelerometer state publication failed`，具体成因未独立排查，不能把它直接认定为栅栏失效或已解决。后续每轮先选一个可复现的小任务，例如自动旋转或暂停恢复，建立失败证据后最小修改并验证；不要为尚未提出的传感器扩展一次性改动多个方向。
- AEPlus 公开主分支及此前 `d470c4d` 基线的宿主桥接与 HAL 源码仅列出加速度计：`https://github.com/somerandomusernam2/AEmulator-Plus/blob/d470c4db72c988ebfd7e16c6598dd1b48595566c/app/src/main/java/app/aemu/core/Sensors.kt` 与 `https://github.com/somerandomusernam2/AEmulator-Plus/blob/d470c4db72c988ebfd7e16c6598dd1b48595566c/native/sensorshal/sensors_hal.c`。这是源码对照，不是 AEPlus 或其他 Guest 的实机全面验收，也不自动同步上游。

#### 音频索引与快速关机（2026-11-12）

- 回归现象：开机后立即关机会出现 `system audio indexing failed: IllegalStateException`。日志显示索引期间发生关机；现有生命周期保护在关机后拒绝启动新命令，原流程将该拒绝作为普通失败记录。使用可区分的停止异常，仅将此类拒绝识别为取消，其他异常仍报告失败。
- 新增测试复现扫描与验证之间关机，以及已进入工作线程但命令尚未创建时关机；修复前两项失败，修复后连同生命周期及索引测试通过。已构建并安装 standard Debug；实机启动完成后立即从宿主菜单关机，索引等待阶段记录取消且未出现该异常。再次启动后系统音频索引完成。实机未保证命中每种并发时序；命令执行中被终止的其他失败仍需按实际记录排查。回滚本次修复可撤销 `VmLifecycle` 的专用停止异常和 `TargetRingtoneIndex` 对该异常的处理，不涉及原始固件或运行树修改。

#### 客体刷新率页面诊断（2026-11-12）

- 回归记录显示原厂屏幕刷新率页面为空。只读检查该次运行时，Guest 日志报告支持的刷新率仅为约 60 Hz，设置页记录支持模式数量为 1；原厂设置页的实现对少于两个模式不生成刷新率选项。客体当前使用的 composer 进程映射了模拟器 `hwcomposer.default.so`，其摘要与仓库资产及用现有源码重建的产物一致；该源码只提供一个约 60 Hz 的显示配置和对应的时序。
- 这解释了页面为空。VSync 控制与 Guest 多刷新率模式是不同链路。本轮仅做只读诊断，未修改固件或安装应用；要支持真实切换仍需设计多模式、切换时序与原厂设置链路并实机验证，不能仅添加外观选项。

#### NX809J 显示模式组件验证（2026-11-12）

- 红魔 11 Pro 的固定档位为 60、90、120、144 Hz，另有智能调节；实机规格参考 `https://www.gsmarena.com/nubia_redmagic_11_pro-review-2902p3.php`。原厂设置代码读取支持模式并将选择写入 `Settings.System.refresh_rate_mode`，后续消费者和完整切换链路仍待确认。
- 本轮只实现独立的显示组件阶段：`native/hwchal/hwc.c` 中以 `AEMU_NX809J_DYNAMIC_REFRESH` 条件编译提供四种配置、HWC 1.4 配置切换接口和对应的 VSync 周期；未启用该宏时仍为原来的 HWC 1.2。未修改 ROM、应用资产和宿主显示设置，也未构建或安装 APK；现有 Guest 尚不会获得多档刷新率。
- 先添加 `native/hwchal/tests/nx809j_refresh_test.c`，原代码因缺少切换接口无法编译；实现后用 NDK 28.2 交叉编译，在连接的 Android 手机运行独立测试，验证四档配置、非法输入、配置数量查询以及四档回调时间戳与实际间隔，进程退出码为 0。测试未启动 Guest。
- 含宏与不含宏的共享库均在临时目录编译成功；不含宏的构建经 strip 后 SHA-256 与现有 n64-36 资产一致，均为 `27d54dd26dcc0fc66b6a7d321f8f754bee8deccd2adb6d37418685a0cc01e0db`。测试命令可用 NDK clang `--target=aarch64-linux-android26 -O2 -Wall -Wextra` 编译测试文件后在 ARM64 Android 上运行；回滚本阶段只需移除该宏包围的新增代码和测试文件，不涉及原厂固件或已安装应用。
- 未完成：将新组件接入仅限目标 Guest 的可重建产物、确认原厂设置的选择如何触发 SurfaceFlinger 模式变更、验证智能调节、宿主刷新率/渲染链路及不同宿主能力。客体内部 144 Hz 时序不保证宿主屏幕实际以 144 Hz 显示，需实测后再交付。

#### 多档显示组件的画面裁剪兼容（2026-11-12）

- 只读检查目标 Guest 的 `services.jar`：原厂 `GlobalSettingFpsController` 监听 `refresh_rate_mode` 并转换 60/90/120/144 Hz；`ScreenRefreshRateController` 从 SurfaceFlinger 获取支持模式，并在切换路径调用 `SurfaceControl.setDesiredDisplayModeSpecs`。这是固件代码证据，尚非启用多模式后的实机验证；原厂还监听 LCD 事件并可能检查 `/proc/driver/lcd_fps`，其在模拟环境中的行为未验证。
- 对照 Android HWC 接口和 HWC2On1 适配器发现，1.3 及以后传递浮点裁剪坐标；上轮实验性的 HWC 1.4 组件仍按旧整数格式读取。新增 `nx809j_crop_test.c`，修复前在手机独立运行退出码为 1，不能识别正常图层。
- 在多档条件编译路径中正确读取整数值浮点坐标，并拒绝小数、负数和越界值交由系统合成；复制画面时也使用转换后的坐标。修复后裁剪测试和原有四档时序测试在手机均退出 0；两种共享库均可交叉编译，不启用实验宏的构建经 strip 后仍与原资产 SHA-256 一致。该测试没有启动或修改 Guest，尚未验证实际帧显示。
- 本轮未改 ROM、应用资产、已安装应用或宿主显示设置。回滚本阶段可去除 `hwc.c` 中本阶段的浮点坐标兼容代码及 `nx809j_crop_test.c`，保留此前多档组件；后续仍需目标专用产物接入与客体端到端验证。

#### NX809J 多档组件接入及打包（2026-11-12）

- 新增 `native/hwchal/build_nx809j.py`，使用 NDK 28.2.13676358 和 `AEMU_NX809J_DYNAMIC_REFRESH` 从当前源码构建目标专用 `engines/redmagic/hwcomposer-refresh.so`；基线 `engines/n64-36/vendor#lib64#hw#hwcomposer.default.so` 保留。产物 SHA-256 为 `118109d9ec856455ffafd4362a9bcff1e10db368a707271d7352a1cd8dd6f20f`。重建命令：`python native/hwchal/build_nx809j.py --ndk <NDK目录>`。
- `TreeFixer` 的引擎文件选择对 API 36、NX809J、ARM64、N64 同时匹配时改用目标资产，其他组合保留原资产。先加选择测试时因类不存在编译失败，接入后 `:app:testStandardDebugUnitTest --tests app.aemu.core.TargetRefreshComposerTest` 通过。
- 构建 `:app:assembleStandardDebug` 成功，并从 APK 的引擎索引和 blob 验证目标资产内容与源码产物逐字节一致。独立 ARM64 测试在手机宿主进程 `dlopen` 该资产并检查 HMI 模块入口，退出码 0；没有打开 framebuffer、没有运行 Guest composer，不能视为客体实际加载或切换通过。
- 本轮未安装 APK、重启 Guest、替换手机运行树或修改原始固件。下轮仍需验证 Guest 真正加载该组件、显示四个固定档位及逐档切换和智能调节，并核实宿主显示链路；运行时效果与稳定性未知。回滚接入可移除 `TreeFixer` 的目标选择及 `TargetRefreshComposer.kt`，重新构建并在 Guest 停止后启动时由现有资产安装逻辑恢复 n64-36 基线；同时移除本轮目标资产、构建脚本和测试，不修改原厂固件。

#### NX809J 客体多档切换实测（2026-11-12）

- 经 Guest `sync` 后触发关机，确认目标 Guest 引擎及 binder 进程退出；保存安装前应用 APK 到 `%TEMP%/rme-before-refresh-install.apk`，SHA-256 `4a81dcaf8d5e9025247d859bc8cb6d2c491fd7ca7bc126b6fd28068f6b18740c`。随后安装已构建的 standard Debug APK，SHA-256 `c9d4f27b0b056e760c1f5b6629b85a8a6725aa799388c534aeb3d8d970c1206a`，并启动原有实例。没有改动原厂固件。
- 客体 composer 进程实际映射 `vendor/lib64/hw/hwcomposer.default.so`，运行树该文件 SHA-256 为 `118109d9ec856455ffafd4362a9bcff1e10db368a707271d7352a1cd8dd6f20f`，与目标资产一致。客体启动完成，SurfaceFlinger 的 HWC2On1Adapter 报告 HWC 1.4 和四个模式；原厂 `all_refresh_rate` 为 `auto,60,90,120,144`。
- 通过 Guest 设置接口依次写入 `refresh_rate_mode` 1、2、3、4，分别等待后读取 SurfaceFlinger 及 HWC2On1 状态：活动模式依次为 60、90、120、144 Hz；验证后恢复原值 0（智能调节），当时活动模式为 120 Hz。固定档位的切换已覆盖设置存储、原厂系统服务、SurfaceFlinger 及适配器；未对长时间运行、负载变化、各档实际应用帧率或宿主实际面板刷新率进行完整测量，不能将 Guest 的 144 Hz 报告等同于宿主物理 144 Hz。自动模式仅验证可选回并观察一次活动模式，未验证全部动态策略。
- 已发起打开原厂刷新率设置页面；手机宿主出现旧版应用兼容提示且 Guest 处于锁屏，待关闭提示并解锁 Guest 后目视验收。Guest `uiautomator dump` 被终止，未拿到页面控件树，不将其当作页面验证成功。测试 APK 已留在手机且 Guest 正在运行；若需恢复，在 Guest 正常停止后用上述安装前 APK 覆盖安装，运行树 HAL 由旧版启动时按其引擎资产恢复。

#### 0.4.0 验收范围（2026-11-12）

- 0.4.0 除相机外的现有功能按当前范围通过验收，包括主题与 ztecfg、基带、联网、传感器、振动、显示刷新率及其他已讨论功能；相机留待后续处理。
- 该结论不增加新的测试、构建、安装或实机结果；本记录之前各模块的适用条件、未验证事项、未接入原型和新实例实测限制仍然有效。原厂相机预览渲染闪退及其他相机验收项仍未完成，详见 `native/camerahal/NX809J_CAMERA_COMPAT.md`。

#### 原厂相机预览实验与存储诊断（2026-11-15）

- 在原有标准包和实例中安装图形桥实验 APK（`%TEMP%/rme-oes-fbo-DIAGNOSTIC-_985wc4p/diagnostic-standard-debug.apk`，SHA-256 `db4a2d1d424e95f13efe77cf4d3463096b6164b85340e2ad45abb1704f99b3ce`）。通过 EMU 资产更新运行副本，未修改原始固件；安装前正常停止 Guest。工作区正式图形桥源码及资产未并入此实验原型。
- 实验前，同一 Guest 的独立图形探针复现外部纹理附着 `0x502`、帧缓冲 `0x8cd7`；实验包中，两次新建缓冲的外部纹理附着及两帧不同四色图案的 GPU、OES、CPU 与重复 `lockPlanes` 读取均通过。该探针未覆盖有填充行、真实相机或断连故障。截图显示原厂相机真实画面，前后摄预览正常；日志记录前后摄被打开并持续送帧。该结果只适用于当前实验包、实例及宿主。
- 相机普通拍照提示“存储准备中”，当时照片保存结果未确认。Guest 日志和原厂代码显示，相机将主外部存储判为可移除后寻找第二存储路径，未找到后认为手机存储不可写；系统转储显示模拟主卷已挂载。这不证明实际空间不足或媒体服务完全正常。目标 ROM 的 `VolumeInfo.isPrimaryEmulatedForUser(0)` 要求卷 ID 为 `emulated;0`，当前 EMU `VoldStub` 宣告 `emulated`；两者关系及最小修复仍待通过目标 ROM 卷构造逻辑和实机验证，尚未修改该声明。
- 实验版旧读回协议仍无法区分无更新与失败，断线、帧身份、部分纹理重绑/生命周期和原厂锁失败处理未闭环，不作为正式修复。标准 Camera2 拍照、可解码 JPEG、关闭重开、占用或权限恢复及原厂拍照均未完成验收。原厂录像和高级模式未验证。
- 实验 APK 安装前的标准包备份与回滚步骤记录在 `tools/diagnostics/oes-fbo-prototype/README.md`；备份位于系统临时目录，可能被系统清理。设备测试暂停后未继续操作；再次操作前须重新确认连接、包和实例状态，不应假定手机当前在线。

#### 原厂相机拍照实验（2026-11-15）

- 设备恢复连接后，在目标 ROM 的存储服务中确认已挂载主模拟卷被错误标记为可移除：系统身份探针报告 `primary=true`、`emulated=true`、`removable=true`，卷 ID 为 `emulated`。目标 ROM 的 `VolumeInfo` 使用 `emulated;0` 判断用户 0 的主模拟卷。新增目标专用 VoldStub JAR 与安装/启动选择；共用 JAR 和其他版本保持原路径。Binder 事件定向测试及目标资产测试通过；实验包安装到原有标准包和实例后，探针确认 `emulated;0` 已挂载且 `removable=false`，原厂相机的存储容量从错误值变为正值。
- 第一版元数据实验在启动时暴露未注册的 `org.quic.camera2.statsconfigs.AECLuxIndex`，造成原厂相机再次闪退；先恢复到前后预览可用的测试包，再在厂商表末尾注册可为空的 Float 键，不虚构亮度。普通拍照前一轮还因结果缺少 `SENSOR_EXPOSURE_TIME` 而崩溃；源码和最终实验 HAL 提供曝光 33,333,333 ns、ISO 100、后处理增益 100 的虚拟标称结果，并声明对应键。这些不是宿主实际测量值，真实宿主曝光和 ISO 传递仍未实现。厂商键和元数据定向测试通过。
- 最终实验 APK `%TEMP%/rme-oes-fbo-DIAGNOSTIC-_985wc4p/camera-lux-diagnostic.apk`（SHA-256 `cc986cd19d6a7dc89d323e943533fada7b01cd2a7ed67d71fa35dc300709f132`）已覆盖安装，保留同一实例。包内存储 JAR、图形桥两端及相机 HAL 资源摘要已核对；Guest 两处运行时 HAL 为 `0c11dba52649295386f4cae5c4bcb593694e6117bd644e35434c0208e77dfa12`。本机选定 Gradle 单测、APK 构建和原生定向测试通过。
- 实验包中原厂相机启动后，自动点击一次普通快门，日志依次记录 JPEG 回调和 `onStorageFinish SUCCESS`；本次生成的单个 JPEG 文件存在，媒体库元数据报告 640×534、53,619 字节。截图确认原厂图库能打开照片。该截图未独立证明输出方向或颜色在所有场景正确；标准 Camera2 拍照、关闭重开、权限/占用恢复、其他宿主和新实例仍未逐项验证。
- 图形桥仍为临时诊断原型，未正式接入工作区资产，断线、旧帧、帧身份及部分纹理生命周期问题未闭环；实验 HAL 从临时源码构建，仅用于隔离结果元数据，保留此前已安装版本的请求行为，并未带入工作区的无新帧错误处理。不要将此实验 APK 视为完整相机正式交付。原厂录像和高级模式未验证。回滚前先正常停止 Guest，实验包和安装前备份位置见 `tools/diagnostics/oes-fbo-prototype/README.md`。

#### 同一实验包新实例相机验证（2026-11-15）

- 本轮选择卸载重装以验证新实例，不保留旧实例；正常停止后卸载标准包，旧应用私有实例随之删除。初次 ADB 安装被系统取消，随后使用设备已有的 Root 权限安装相同实验 APK（`cc986cd19d6a7dc89d323e943533fada7b01cd2a7ed67d71fa35dc300709f132`）。导入输入为设备 Download 目录中的 `RedMagic_NX809J_VM_ztecfg_codex_20261109_6e4c9f2a.zip`，以系统文件选择器导入新实例 `j2fxy4`；导入器识别 NX809J、Android 16/API 36、ARM64，首次启动 `sys.boot_completed=1`。该 `_VM_ztecfg` 文件是本次导入输入，本次未重新核对固件摘要。
- 新实例运行树的 system/vendor 相机 HAL 均为 `0c11dba52649295386f4cae5c4bcb593694e6117bd644e35434c0208e77dfa12`，目标 Vold JAR 为 `d7c60ff8a88ce71e7546ace75c2f852c0d900886bc79a9776be19e140e980740`，与实验包资源一致。独立 GPU 探针的 OES 附着、两次缓冲生命周期、两帧变化图案和 CPU 读回通过。
- 新实例实机验收确认相机可用、普通拍照保存成功，DCIM 可见照片。只读日志记录 guest cameraId 0 会话成功、JPEG 回调和 `onStorageFinish SUCCESS`，之后该后摄多次关闭重开且会话成功；本次核查范围内无原厂相机 FATAL。此次日志未确认前摄切换、照片方向和颜色；标准 Camera2 应用的拍照、权限拒绝/占用恢复、其他宿主及长期运行仍未完成验收。上述结果只证明当前实验 APK 和该新实例；旧帧/断线及资源生命周期限制仍然存在。
- 补充实机验收覆盖新实例前后摄切换及浏览器调用相机；具体浏览器、网页及调用 API 未逐项记录。权限拒绝不在本轮范围。其他宿主、占用和断连恢复仍未实测。

#### 相机验收（2026-11-15）

- 相机已按当前使用范围通过实机验收。此前已记录的新实例前后摄、浏览器调用、原厂普通拍照与图库/DCIM 验证仍作为具体证据；本次验收未逐项说明新测试的 APK、实例或操作，不新增逐项实测结论。
- 图形桥实验资产尚未并入普通工作区正式构建，断线、旧帧、资源生命周期、占用恢复和其他宿主等既有未验证项不因此视为通过。权限拒绝场景不在本轮验收范围。

#### 0.5.0 依赖与资产裁剪（2026-11-15）

- 审计目标 Guest 的启动与运行依赖，按审计结果裁剪旧资产。内部 `app.aemu` 命名、Guest 原厂服务标识、`applicationId` 和签名身份属于兼容约束。变更、验证和备份见 [0.5.0 迁移清单](MIGRATION-0.5.0.md)。
- 前一验收测试包为 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-beta-graphics-pruned.apk`，SHA-256 `070f18d8c8c5de9b164984868dad4c4f7f578d8ae08f405a56711e9ac7859fee`。回退后 18:39:15 Keystore2 启动、18:39:23 zygote 启动、18:39:39 系统报告启动完成，截至 18:41:12 未见二者退出。当前设备连接、安装及运行状态可能变化，本次交接未重新查询；不要把这组当时日志当作全部功能验证。
- 设备回归发现删除 9 个旧图形资产后原厂相机直接崩溃、无法打开；这些资产已从裁剪候选移出并标记为受保护运行资产，后续测试包必须保留。两项铃声原型裁剪包含在同一基础回归记录中的包内；具体安装、启动及铃声/相机测试范围未逐项展开，不据此扩大到完整兼容验收。
- 后续批量裁剪加 R8 的 `RedMagicEMU-0.5.0-beta-trim-r8.apk` 实机出现 zygote 137 循环，Guest 现场记录 `system_server` 因无法连接 Keystore 服务崩溃；根因尚未定位，不能归因于某个资产或 R8。该轮 24 个资产、旧导入链和 R8 配置已从精确备份安全撤回，不要使用或继续扩展这份失败包；不要清除实例、用户数据、重导入或重置来掩盖问题。恢复旧版后日志有上述恢复迹象；若再现先保存现场、定位或安全回滚，不继续裁剪。
- 随后只裁剪未接入运行链的 `FirstRingtoneInitState.kt` 及其专用测试，实际 `FirstRingtoneInitStore` 和铃声链保留。原件及 SHA-256 清单位于 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-ringtone-prototype-prune\`。测试包为 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-beta-ringtone-clean.apk`，SHA-256 `f261d523e9e259cc277be74847080f7361ba562b09fe4fbe339bc3c81112419c`；比上一包少 4,096 字节，包名 `app.redmagicemu`、版本 `0.5.0-beta`、versionCode 8 与调试证书不变。设备验收记录表明该包完成一次基础回归，但未逐项列明范围。
- 该次 WSL `:app:testStandardDebugUnitTest :app:assembleStandardDebug --rerun-tasks` 成功，701 项、0 失败、3 跳过；与稳定包比较只有 `classes4.dex` 内容不同，其余非签名 APK 内容逐项相同。归一化反汇编中恰好移除原型及 3 个内部类，其他 692 类无差异。
- 相机已按现有使用范围验收，但普通工作区 4 份图形/HAL 二进制与验收包不一致。经过验证的隔离 WSL 构建快照为 `/home/<user>/.cache/rme-wsl-build-EOnMk0DP/project-050-ringtone-clean`；此前基线为 `project-050-graphics`，两者依赖相机验收二进制，不可直接以普通工作区构建声称复现。相机断线、旧帧、资源生命周期及其他宿主等限制保留。APK 调试证书 SHA-256 为 `494eb713e0b14a19cb11d08e8cabb0a0b0fc25c341e57eaf0581c122527be389`；任何后续测试包需核对包名和证书，不改变发布签名。
- 本轮范围调整为只删除不可用的 `ImageStore.delete` 桩及 `InstanceDeletionTest`，保留 `ImageStore.resetData` 和 `InstanceResetTest`。隔离 WSL 构建与完整测试成功，700 项、0 失败、0 错误、3 跳过；测试 APK 为 `E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-beta-delete-stub-clean.apk`，SHA-256 `346b0826069064b83302aa52de8a6fcfa23ed27ecba9f3ef03e0a16301d8ee1f`。与铃声包相比仅 `classes4.dex` 变化，`engines.idx` 相同，9 个受保护图形资产逐项保留；尚未安装或实机验证。
- 多个未接线实验合并清理时，AOD ART 查找、AOD loader、radio VINTF/canary 三组源码与测试已先备份、校验后归档，记录保留在迁移清单；另清除了 18 个有源码的可重建 Python 缓存。这些整理不减少 APK。后续只选有明确依赖证据、可独立验证的小任务；不要为了删 `aemu`、32 位或实验名而破坏运行链、版权、许可和来源。

#### 0.5.0 裁剪完成与验收包（2026-11-15）

- 0.5.0 裁剪到此结束，`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\RedMagicEMU-0.5.0-beta-delete-stub-clean.apk` 为当前最终测试包；包大小 43,632,245 字节，SHA-256 `346b0826069064b83302aa52de8a6fcfa23ed27ecba9f3ef03e0a16301d8ee1f`，调试证书 SHA-256 `494eb713e0b14a19cb11d08e8cabb0a0b0fc25c341e57eaf0581c122527be389`，v2/v3 签名和 16 KB 对齐检查通过。该结论是包选择，不替代逐项设备回归记录。
- 后续只读审计确认：Compose `ui-tooling` 隔离移除可省 116,948 字节；旧通用导入链及 Brotli/Junrar 隔离截断可省 238,676 字节；`material-icons-extended` 以占位图标替换的隔离上限可省 6,720,704 字节，但探测版图标视觉被替换，不能交付，正式 43 图标子集未实现。上述探测均只发生在临时 WSL 快照，未改变最终 APK 或产品源码。
- `libclang_rt.ubsan_standalone-aarch64-android.so` 不能按“未使用”裁剪：`libeffects.so`、`libeffectsconfig.so`、`libkeymaster3device.so` 的目标 ELF `DT_NEEDED` 明确引用它。9 个旧图形资产和 SwiftShader 大资产继续保留；R8 的 zygote/SIG 137 回归仍未闭环，不作为安全裁剪项。
- 此阶段保留现有实例、用户数据和原始固件。后续裁剪按迁移清单逐项复现、备份并记录回滚。

#### 0.6.0 交付概览（2026-11-15）

- 需求：在已验收的 0.5.0 基础上完成实例电池同步与运行时控制、传感器和 GPS 桥接、虚拟 Wi-Fi 断网修复、zygote 稳定性、虚拟 LTE，并回归相机；LTE 相关功能放在最后推进。
- 实现：实例设置提供“跟随宿主/自定义”电池控制及宿主电池广播实时刷新；Guest 浮动菜单提供同一运行态控制。传感器桥接同时覆盖加速度计、陀螺仪、磁力计和宿主位置。虚拟 Wi-Fi 增加网络闸门，LTE 增加 synthetic Radio 数据链路，相机相关图形/HAL 资产恢复到已验收版本。
- 验证：发行配置已收敛为单一 `app.redmagicemu`；WSL 干净克隆执行 `:app:testDebugUnitTest :app:assembleDebug :app:assembleRelease :app:copyReleaseApks`，717 项、0 失败、0 错误、3 跳过，Debug、Release 与发布 APK 复制均成功。实机已覆盖电池插拔刷新、指南针、传感器、虚拟 Wi-Fi 关闭断网、虚拟 LTE 数据链路与前后摄恢复。
- 限制与回滚：GPS 仍来自宿主最后已知位置；LTE 为 synthetic Radio；相机另有浏览器 API、断连恢复及其他宿主边界。各模块的限制和最小回滚范围见对应小节及[兼容矩阵](COMPATIBILITY.md)。

#### 0.6.0 宿主电池同步与 Guest 运行时控制（2026-11-15）

- 电池同步在 `codex/0.6.0-battery-sync` 阶段分支实现，虚拟 LTE 在 0.6.0 的最后阶段接入。
- 新增每实例 `batterySync` 设置，默认开启，旧实例缺少该字段时仍按开启读取；主页实例设置的“操作”区提供开关，关闭后宿主电池广播不再写入 Guest。Guest 运行后监听宿主 `ACTION_BATTERY_CHANGED`，通过 `dumpsys battery` 同步电量、充电状态和 AC/USB/无线供电来源。
- 初版交互将原独立电池弹窗移除，把电量滑杆、未充电/AC/USB/无线选择、应用和恢复直接放入 Guest 运行时右上角浮动菜单展开的侧栏。手动修改立即可见；宿主同步开启时，后续宿主状态变化可再次覆盖手动值。该顶部菜单位置随后在“电池菜单交互微调”中改为普通按钮加弹窗，实例设置中的内联控制保留。
- 目标单元测试 `HostBatterySyncTest`、`GuestBatteryControlTest` 通过。Windows 完整单测运行 710 项，89 项按既有环境条件跳过，仅 `InstanceRuntimeLeaseTest.anotherProcessBlocksAndDeathReleasesLock` 因跨进程文件锁未释放失败；为避免该环境差异，随后在 WSL 隔离副本 `/home/<user>/.cache/rme-wsl-build-EOnMk0DP/project-060-battery-B5SydLHj` 运行同一命令，710 项、0 失败、0 错误、3 跳过，`assembleStandardDebug` 成功。WSL APK SHA-256 为 `8fada82fd7212e237a941184f59cf56811d9e5bf33d665ba21af91220ef1359d`，大小 43,570,140 字节；Windows 构建的同一源码 APK 为 `app\build\outputs\apk\standard\debug\app-standard-debug.apk`，仅作交叉构建验证。
- 已只读确认无线 ADB 设备在线且当前安装的是 0.5.0-beta；本轮没有停止正在运行的 Guest、覆盖安装或修改实例数据。侧栏在横竖屏/小屏的实际布局、触控、宿主电量与充电来源变化、关闭同步后的手动保持及真实 `dumpsys battery set wireless` 行为均未实机验证。
- 回滚本轮电池功能可移除 `HostBatterySync.kt`、`GuestBatteryControl.kt` 和对应测试，撤销 `VmActivity` 侧栏接线、`SettingsSheet`/`GuestImage` 的 `batterySync` 字段、字符串及本地化源；不会影响原始固件、已导入实例或其他未提交改动。

#### 0.6.0 运动传感器扩展（源码、单测与 APK 打包完成，未实机）

- 目标 AEM2 协议从单一加速度计扩展为保留类型字段的加速度计（1）、陀螺仪（4）和磁场（2）；宿主仅在 Guest 画面可见且桥接开启时注册可用的三类默认传感器，三类事件分别按时间戳过滤并写入同一 FIFO。
- `n64_sensors_hal.c` 枚举三类 sensor_t，使用独立主动状态、采样周期、时间戳和 flush 边界；每类可独立启停、设置周期和 flush，未知类型及未激活类型不输出。坐标向量继续使用宿主自然屏到 Guest 固定竖屏的轴映射；陀螺仪和磁场沿用同一向量变换。
- 先增加了类型保留与三传感器 HAL 契约的失败测试，修复后 Kotlin 定向测试及 WSL C 测试 `n64_hal_test` 均通过。使用 NDK 28.2.13676358 重建 `n64-36` system/vendor 两份 HAL 资产，SHA-256 均为 `49b85c6411cd232ee44fb7ff42d76367734a27cd37698c0591a72f5528ab1ff9`；生成的 Debug APK 内对应 blob 摘要一致。
- 尚未安装或启动目标 Guest。普通应用枚举、三类真实数据与单位/方向、精度、暂停恢复、独立启停与 flush、服务重启及不同宿主传感器规格均未实机验证；声明的量程/分辨率为虚拟接口值。
- 回滚需同时撤销主客 AEM2 多类型接线、重建两份 `n64-36` HAL 资产并重新构建/替换运行时副本；不得只回滚 FIFO 一端。原始固件未修改。

#### 0.6.0 虚拟 Wi-Fi 关闭后仍可联网（2026-11-15）

- 在设备实例 `j2fxy4` 的现有 0.5.0-beta 虚拟 Wi-Fi 环境复现：通过 Guest `IWifiManager` 事务 42 关闭 Wi-Fi 后，`dumpsys connectivity` 报告 `Active default network: none`、`Current Networks:` 为空，但 Guest 内 `ping -c 1 -W 2 8.8.8.8` 仍收到 1 个包且退出码为 0。随后通过事务 42 重新开启 Wi-Fi，默认网络 102 恢复。该结果证明仅注销 NetworkAgent 不会阻止 QEMU 经宿主直接建立的 Internet 套接字；诊断结束时已恢复 Wi-Fi 开启状态，未停止 Guest 或安装新 APK。
- `TreeFixer` 对目标虚拟 Wi-Fi 在每次启动时原子写入 `/dev/aemu_network_gate = 0\n`，切回以太网时删除旧标记，避免上一轮状态泄漏。`WifiConnectionCoordinator` 在注销网络前关闭标记，只在 NetworkAgent 注册成功后打开；启动和异常路径保持 fail-closed。
- API 36 引擎在 `connect`、`sendto`、无地址 `sendmsg`、`writev` 路径读取该单字节标记。只有恰好 `1` 允许外部 IPv4/IPv6 流量，`0`、空文件或畸形内容拒绝；Unix socket、netlink 与回环地址不经过此 Internet 闸门。缺失标记表示以太网模式，保持原行为。
- 新增并运行 `wifi_network_gate_test.py`（WSL 编译实际决策函数并检查四条发送路径接线）、`WifiNetworkGateTest`、`WifiConnectionCoordinatorTest`，以及既有 `build_service_test.py`、`network_agent_test.py`、`wifi_disabled_startup_test.py`、`wifi_state_transition_persistence_test.py`、`wifi_launch_route_test.py`，均通过。
- WSL 隔离副本重新执行完整 `:app:testStandardDebugUnitTest :app:assembleStandardDebug`，711 项、0 失败、0 错误、3 跳过。重建 `libaemu64_36.so` 的 SHA-256 为 `e9a1a6f567a1a745c1a9bd219369b1cf55e649f73404355ef95cfa6c51568e58`；`wifi-service.jar` 为 `2c6f70cfa1549ac90876f89ef2f1d4016178ac9298f18071cca5452e8797f6f3`；APK 内对应 lib/blob 摘要一致。最终 WSL APK SHA-256 为 `73501d268ee53eca94b0eaa8197ebbd78d73fb8cca9f8e75ffa0e93e0b0471ad`，大小 44,320,007 字节；Windows 可访问副本为 `app/build/outputs/apk/standard/debug/app-standard-debug-wsl.apk`。
- 该中间 APK 未单独覆盖安装；后续 0.6.0 恢复包已在目标 Guest 完成实机验收，关闭虚拟 Wi-Fi 后的联网问题与 zygote 回归均已修复。具体 ping/浏览器及重新开启步骤未另存逐项日志；验收时 Guest 数据未清除。
- 回滚需同时撤销 `TargetNetworkGate`、`WifiNetworkGate` 接线、引擎网络闸门和对应测试，并恢复本轮前的 `libaemu64_36.so`、`wifi-service.jar` 后重新构建；只回滚一端会分别造成联网泄漏或 Wi-Fi 永远不可用。原始固件未修改。

#### WSL APK 签名与覆盖安装兼容（2026-11-15）

- 覆盖安装回归发现 WSL APK 无法升级已安装包。核对得到设备已安装包和上一通过包证书 SHA-256 均为 `494eb713e0b14a19cb11d08e8cabb0a0b0fc25c341e57eaf0581c122527be389`；WSL 默认 `~/.android/debug.keystore` 构建出的 APK 为 `31e33d5612b30489607a18d69dcb1e96ba343d841d05957614088d2124d9a0db`，因此系统正确拒绝覆盖。
- 运行 `apksigner` 时使用 Windows 现有 `%USERPROFILE%\.android\debug.keystore`（别名 `androiddebugkey`）对 WSL 产物重新签名，未修改业务内容。重新签名后的 v2/v3 验证通过，证书恢复为设备一致的 `494eb7…527be389`；`zipalign -c -P 16 -v 4` 成功。与原 WSL APK 比较，除签名相关 `META-INF` 条目外 404 个条目名称和内容逐项一致；包名 `app.redmagicemu`、versionCode 8、versionName 0.5.0-beta 不变。
- 可覆盖安装测试包为 `app/build/outputs/apk/standard/debug/app-standard-debug-wsl-resigned.apk`，44,369,525 字节，SHA-256 `aa596ac982bd9951181819e9fbf10a8e787733069b8b3198f8e859c8836bbf1a`。本轮没有直接安装；安装后仍需验证顶部菜单电池侧栏、宿主同步开关及虚拟 Wi-Fi 关闭断网。调试密钥未复制到仓库。
- 后续 WSL 构建若仍需覆盖现有包，构建后必须继续用同一现有调试密钥重签，不能直接使用 WSL 默认 debug keystore；不要为通过安装而卸载应用或清空实例。

#### 电池菜单交互微调与重新签名包（2026-11-15）

- 顶部浮动菜单不再内嵌整块电池控件，而是恢复为与其他项目一致的普通“电池”按钮；点击后打开独立弹窗。弹窗先选择“跟随宿主”或“自定义”，只有自定义模式显示电量滑杆、未充电/AC/USB/无线和恢复按钮。
- 模式按实例持久化并与主页“电池状态跟随宿主”开关共用 `batterySync`。切回宿主模式时会立即请求一次宿主状态应用，不必等待下一次电量变化；切到自定义模式先关闭自动同步再应用手动状态。
- WSL 完整 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 再次通过：711 项、0 失败、0 错误、3 跳过。新 APK 已使用设备现有调试证书重新签名，v2/v3 和 16 KB 对齐检查通过；除签名条目外 404 个条目与原 WSL 构建逐项一致，包名和版本仍为 `app.redmagicemu`、versionCode 8、0.5.0-beta。
- 当前可覆盖安装包为 `app/build/outputs/apk/standard/debug/app-standard-debug-wsl-signed.apk`，44,414,581 字节，SHA-256 `6df4bdfedb06a28383460422e1d732861be11ab204cdfdc66c9af9fe041573cc`。尚未安装；弹窗布局、宿主/自定义切换、即时宿主刷新和手动状态仍需实机验收。

#### zygote FD allowlist 崩溃修复（2026-11-15）

- 两份宿主日志均显示 zygote 启动约 2 秒后退出 `134`。Guest `run/zygote.log` 给出直接证据：`JNI FatalError called: (system_server) Not allowlisted (6): /dev/aemu_network_g…`，随后可见完整字符串 `aemu_network_gate` 和 `zygote64`。退出码 134 即 `128 + SIGABRT`。
- 根因是上一版网络闸门在 `run64.c` 中首次打开 `/dev/aemu_network_gate` 后把 FD 缓存在 `g_network_gate_fd`。虽然使用 `O_CLOEXEC`，zygote fork system_server 不经过 exec，FD 6 仍被 system_server 继承并由 Android zygote FD allowlist 检出不合法，触发 JNI FatalError 和 abort。这是上一版网络闸门引入的明确回归，不是当前 `ISensors` 噪声或普通 OOM。
- 修复为不保留任何闸门 FD：每次检查使用 `openat`、`pread`、`close`，读到精确 `1` 才放行；同时加入源码回归测试，断言不存在 `g_network_gate_fd` 且检查函数包含立即关闭。
- `wifi_network_gate_test.py` 通过；WSL 完整单测和构建通过：711 项、0 失败、0 错误、3 跳过。新引擎 `libaemu64_36.so` SHA-256 为 `04149a06456b438531f5bd7580d1d1b7d69352d8e3a6b4046ec78e9a30827a81`，APK 内摘要一致。
- 新可覆盖安装包为 `app/build/outputs/apk/standard/debug/app-standard-debug-zygote-fix-signed.apk`，44,414,581 字节，SHA-256 `dde6c7efdb987b98324c7042510e3053334f1589586c7acb51c8da8503819b37`；使用设备一致的调试证书签名，v2/v3 和 16 KB 对齐通过，除签名条目外 404 个 APK 条目与 WSL 构建逐项一致。
- 该修复包当时尚未单独安装；后续 0.6.0 恢复包已覆盖安装并通过实机验收，zygote/system_server 持续运行，关闭虚拟 Wi-Fi 后外部 Internet 发送被阻断。

#### 0.6.0 虚拟 LTE 联网（2026-11-15）

- 目标实例网络模式新增 `LTE`，与 Ethernet、虚拟 Wi-Fi 共用一个按实例保存的选择。LTE 模式不再启动 Ethernet 占位网络；Radio 进程显式带 `--lte-data`，由 Telephony 根据 synthetic Radio data 返回的 LinkProperties 创建 `CELLULAR` 默认网络。
- `RadioDataService` 仅在 opt-in LTE 模式响应真实数据调用：返回 active IPv4 bearer、`eth0`、`10.0.2.15/24`、网关 `10.0.2.2`、DNS `8.8.8.8`/`1.1.1.1`、MTU 1500 和调用方 PDU session id；`getDataCallList`、`deactivateDataCall`、`setDataAllowed`、data profile 与 PDU session 操作已接线。默认非 LTE 模式仍保持原 `REQUEST_NOT_SUPPORTED` 合同。
- 目标 ROM 的 product APN 配置包含 `46000 / mnc 00` 的 `cmnet` default APN。移动数据关闭、Radio OFF/断开、callback 死亡、数据调用释放或 Radio 服务正常关闭都会移除数据调用并写入网络闸门 `0`；成功 setup 后才写入 `1`。Radio 服务异常退出时 Guest watchdog 也会 fail-closed 关闭闸门。LTE 不读取或连接宿主真实蜂窝网络，仍通过现有宿主 Internet 通道。
- RadioNetwork 在 synthetic ON 状态声明仅支持 LTE allowed-network bitmap，同时保留现有固定 LTE 注册与信号。RadioConfig 的 phone capability 在 LTE 模式声明 `maxActiveData=1`、`maxActiveInternetData=1`，默认模式仍为 0。
- 离线 Radio 回归 `OFFLINE_RADIO_OK dex_classes=482` 通过，新增 `RadioDataLteTest` 覆盖 active bearer 字段、数据列表、移动数据开关、Radio OFF、指示和闸门状态；既有 no-bearer、network signal/allowed bitmap、voice、SMS、config、modem、SIM、event bridge 测试均通过。生产 `radio-service.jar` 为 103 个 DEX 类，SHA-256 `23fac987d7bdf074fc3739fb6a6414de7f89f99832d79cc71d0a8d6dc4c2c742`。
- WSL 完整 `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 通过：712 项、0 失败、0 错误、3 跳过。API 36 引擎仍使用 zygote FD 修复后的 `04149a06456b438531f5bd7580d1d1b7d69352d8e3a6b4046ec78e9a30827a81`。
- 已用设备现有调试证书生成可覆盖安装包 `app/build/outputs/apk/standard/debug/app-standard-debug-lte-final-signed.apk`，44,414,581 字节，SHA-256 `86403048edac7e84e265d9467f44decc461f034116bb1edd12dacd035ddd22d`；v2/v3 和 16 KB 对齐通过，签名证书仍为 `494eb7…527be389`。
- 后续 0.6.0 恢复包已覆盖安装，实机验收确认虚拟 LTE 数据链路已修复。默认网络字段、浏览器/DNS/HTTPS、关闭移动数据、模式往返及飞行模式的具体步骤未另存逐项日志；APN 选择、DataNetwork 默认路由、Telephony 私有策略和原厂设置页在该模拟链路下仍不应视为已完整验证。
- 回滚需撤销 `TargetNetworkMode.LTE`/UI、`TreeFixer` 与 `GuestVm` 的 LTE 接线、`RadioServiceMain`/`SyntheticSingleSlot` data 能力、`RadioDataService`/`RadioNetworkGate` 及测试，恢复本轮前 `radio-service.jar` 并重新构建；同时评估网络闸门与 APK 资产回滚。不要清除实例数据或修改原始固件。

#### NX809J 指南针、运动传感器与宿主位置同步（2026-11-15）

- 回归现象：原厂指南针进入后闪退。`dumpsys activity exit-info` 连续记录 `reason=4 (APP CRASH(EXCEPTION))`；DropBox `data_app_crash` 栈明确为 `requestLocationUpdates("gps")` 后 `IllegalArgumentException: provider "gps" does not exist`，不是磁力计异常。
- 增加仅限 `NX809J/API 36/ARM64/N64` 的 `LocationStub`：以 root 通过 Guest 自身 `ILocationManager` 注册并启用 test provider `gps`，跟随 system_server 更替重试。宿主新增粗略/精确定位权限、运行时申请和 `HostLocationBridge`；有真实宿主位置时原子写入 `dev/aemu_location`，Guest 只转发真实 `Location`，无位置时不制造假坐标。
- 指南针随后不再闪退但仍不动。Guest SensorService 显示三个 AEmulator 传感器为 `continuous | wakeUp`，而 AOSP Orientation 为 non-wake-up；根因是 HAL 把 `SENSOR_FLAG_CONTINUOUS_MODE` 写成 `0x1`，该位实际是 `SENSOR_FLAG_WAKE_UP`。改为 reporting-mode `0` 并补原生测试，确认三类为 `non-wakeUp | continuous`。
- `n64_hal_test.c` 通过；WSL `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 通过。两份 N64 HAL SHA-256 均为 `c35dc35e2d2a1efdb85fee11dc928a0e889d6d7089782bd1a72302672e147e83`，`aemu-stubs.jar` SHA-256 为 `8702b01cff582ead5011304e5d50959ebc992515b3052a3866422d7ae1b05426`。
- 实机验收确认指南针持续显示航向、磁场约 `42 µT`，经纬度显示宿主最后已知网络位置；当前范围标记为完全可用。安装包 `app/build/outputs/apk/standard/debug/app-standard-debug-location-signed.apk` SHA-256 为 `52e594f185f34effb41bfbef69b0f234f0d004caf57d394839e0bea2f8694e91`，使用设备一致调试证书，v2/v3 与 16 KB 对齐通过。
- 限制：经纬度遵循宿主定位权限和最后已知 network/GPS 位置；无海拔数据时指南针仍显示 `--`。回滚需同时撤销 HAL 标志、位置协议/权限/启动接线、两份 HAL 与 `aemu-stubs.jar`，不能只回退一端。

#### 0.6.0 实例设置、传感器桥接与默认显示调整（2026-11-15）

- “传感器桥接”现在同时控制加速度计、陀螺仪、磁力计和宿主位置同步。关闭时 `HostLocationBridge` 注销宿主监听并把 `dev/aemu_location` 置为非活动，Guest GPS test provider 保留但停用；重新开启后恢复。GPS provider 仍保留，避免关闭桥接时原厂指南针重新出现 provider 缺失闪退。
- 实例设置移除“保持屏幕常亮”选项；`keepScreenOn` 数据和运行行为未删除。实例设置中的电池由单一开关改为与运行时菜单一致的内联“跟随宿主/自定义”、电量滑杆和电源状态控件，无弹窗。
- 自定义电量/电源不写入实例配置。当前 VM 内保存最后一次实际应用值：实例设置或浮动菜单修改后，Guest 立即应用，后续实例设置读取该运行态；没有运行态记录时默认 `100% + 交流电充电中`。电池标题、分段按钮和滑杆使用与下方 `ListItem` 相同的 `16dp` 内容缩进，并补足“操作”后的段落间距。
- NX809J 新导入实例的默认显示改为原厂 `1216 × 2688 / 520 DPI` 的 75%，即 `912 × 2016 / 390 DPI`。已有实例保存的宽高 DPI 不迁移，已有自定义值和原厂 100% 预设仍保持不变。
- WSL `:app:testStandardDebugUnitTest :app:assembleStandardDebug` 通过；新增默认显示断言。实机确认实例设置布局、缩进和当前实例 `720 × 1600 / 300 DPI` 保持不变。最终可覆盖安装包为 `app/build/outputs/apk/standard/debug/app-standard-debug-spacing-signed.apk`，SHA-256 `619a46e593b1145bda8c975352cd0849fb42e0eb8b2c8013c68ff86e8da6e69c`，已覆盖安装。传感器桥接关闭后的 Guest GPS 停用、实例设置与浮动菜单电池双向运行态同步仍需实机专项回归。
- 实例设置中的宿主电池原先只读取打开时的一次快照，故插拔电后不刷新。现已动态注册 `ACTION_BATTERY_CHANGED`，打开设置期间实时更新电量和电源来源。实机用系统电池模拟验证：拔电后设置页立即由 `65% · 交流电` 变为 `65% · 未充电`，`dumpsys battery reset` 后立即恢复 `65% · 交流电`；宿主电池状态已复位。

#### 0.6.0 原厂相机闪退只读诊断（2026-11-15）

- 当前安装包 `app-standard-debug-battery-live-signed.apk`（SHA-256 `d8f16fc1f6ff05ff6c56783c9e6cd7130a0e76d76111a9c8cc70c8c2a66dc0a4`）在实例 `vpwzbf` 复现原厂 `com.android.camera` 闪退。Guest DropBox 连续记录同一异常：`HardwareBufferRenderer.onDraw` 到 `GLCanvas.beginRenderTarget`，最终 `GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:8cd7`。
- 宿主相机桥日志同时显示后摄被多次成功打开，说明权限和取流已到达预览渲染阶段；崩溃属于已知的外部纹理/EGLImage framebuffer 图形桥问题，不是本轮电池、GPS、运动传感器或实例设置代码。开机日志另有一次 `vendor.camera-provider` 在 `camx.provider-impl.so` 的 SIGSEGV，需单列排查，不能用它替代上述应用栈。
- 当前正式 0.6.0 图形资产未接入此前仅用于诊断、曾使前后摄预览及一次拍照成功的 OES-FBO 临时修复。是否并入该原型属于独立实现决策；其读回、资源生命周期、断线和帧同步限制仍存在。本轮只做诊断，未改相机源码或重新安装其他 APK。
- 随后对当前 0.6.0 包与已验收的 0.5.0 包做逐条审计：128 个非引擎 APK 路径名称与内容一致；205 个引擎路径集合一致；仅有 8 个引擎条目和 2 个 arm64 JNI 条目变化。其中 6 项是本轮预期的 stubs、传感器、Radio、Wi-Fi 和网络闸门 API 36 引擎，另外 4 项为回退到旧版本的相机相关文件：宿主 `libaemugl.so`、Guest `libGLES_aemu.so`、system/vendor 两份 camera HAL。未发现其他已验收修复丢失。
- 已从 `RedMagicEMU-0.5.0-beta-delete-stub-clean.apk` 原样恢复上述 4 个文件：`libaemugl.so` SHA-256 `990c361f73fe9760a2644d7a139a3e4fe2377f22a054298f87883656b3332533`，Guest GLES blob `db987d74570f64213a7076bb3ae3b83a30c37d30`，两份 camera HAL blob `1c2e48bc07a08d5367c5efdf5456d3b42d6cbfbe`。恢复前文件备份在系统临时目录 `rme-camera-060-before-merge-dc353e4734df43a2865855e946baa3aa`。重构建后与已验收 0.5.0 相比只剩 5 个预期引擎变化和预期的 API 36 网络闸门引擎变化。
- 可覆盖安装包为 `app/build/outputs/apk/standard/debug/app-standard-debug-camera-restored-signed.apk`，SHA-256 `d77b1661929b79a49528a8c33116c793664d6122e69afe4c3e29a6b0670d19b4`；v2/v3 与 16 KB 对齐通过并已覆盖安装。Guest 冷启动后实际打开后摄和前摄，日志各记录连续约 29 帧和宿主亮度变化；前后摄实机回归通过。

#### 单一发行变体与仓库干净构建验证（2026-11-15）

- 发行配置收敛为单一 `app.redmagicemu`，删除 `standard`、`clone` 和 `v0003` flavor 层；仅保留 Debug/Release 构建类型。当前任务为 `:app:testDebugUnitTest`、`:app:assembleDebug`、`:app:assembleRelease` 和 `:app:copyReleaseApks`，不会再生成 clone 或 v0003 APK。
- 删除仓库内硬编码的本机 `org.gradle.java.home`，构建改用环境中的 JDK 21。OMX Manifest 测试改为校验 XML 可解析且不含不可用 OMX 声明，不再绑定受 Git 换行转换影响的文件摘要。
- 从 GitHub `main` 的干净克隆使用 WSL Linux 文件系统、OpenJDK 21.0.2、Android SDK 36 和 Build Tools 35.0.0 执行 `clean :app:testDebugUnitTest :app:assembleDebug :app:assembleRelease :app:copyReleaseApks --rerun-tasks`：95 个任务全部执行，717 项测试、0 失败、0 错误、3 跳过，构建成功。
- 输出仅包含单一应用的 `app-debug.apk`、`app-release-unsigned.apk` 以及 `release-apks/0.5.0-beta/app.redmagicemu.apk`；没有 clone 或 v0003 产物。构建仍有 SDK XML 版本、`extractNativeLibs`、Kotlin 可空/弃用 API 和预编译库无法 strip 的既有警告，但均未阻断编译。

#### 必需语言完整覆盖（2026-11-15）

- 界面语言新增 `en-US`、`en-GB`、`zh-TW` 和 `zh-HK`；语言选择器、Android locale config 与旧 `en` 偏好的 `en-US` 迁移已接通。简体中文补齐 42 条缺失资源，繁体台湾和繁体香港各生成完整 311 条资源，并分别采用台湾与香港地区常用词。
- `app/l10n/keys.py` 已扩展到当前 311 条 key。`gen.py` 改为直接以现有 XML 为翻译源，删除不再使用的旧 `langs/*.py`；`--check` 强制校验通用英文、en-US、en-GB、zh-CN、zh-TW 和 zh-HK 的 key、空值及占位符完整性。
- `RequiredLocaleCoverageTest` 同时校验五种对外语言、语言选择器和 locale config。WSL 完整 `:app:testDebugUnitTest :app:assembleDebug :app:assembleRelease --rerun-tasks` 通过：718 项测试、0 失败、0 错误、3 跳过。其他 16 个可选 locale 及对应资源目录已删除；通用英文只作为缺省回退。
- 2026-11-16 补齐运行态语言匹配：跟随系统时只保留支持表中的语言；未列入表中、空语言或未知旧偏好统一解析为 `en-US`，不再保留未支持 locale 的布局方向。新增 `LanguageFallbackTest` 覆盖显式选择、系统语言、未知语言、旧 `en` 别名和 locale 扩展；版本号提升到 11，供已安装 10 的测试包覆盖升级。
- 同一提交在 WSL 使用 OpenJDK 21 完成 `:app:testDebugUnitTest :app:assembleDebug :app:assembleRelease --rerun-tasks`：722 项测试、0 失败、0 错误、3 跳过，93 个构建任务全部执行。最终可安装 Debug APK 使用原 `494e…e389` 证书重构建，v2 签名与 16 KB 对齐通过。

#### 0.6.1 GLES 缓冲区映射诊断（2026-11-16）

- 安兔兔 3D Lite 12.0.3 的 Unity 子进程曾两次在 `libunity.so` 中向空地址写入并崩溃。已连接设备安装的 EMU 为 0.5.0-beta；其运行时 Guest GLES 桥和宿主 GL 桥摘要分别为 `6cc7f15ce1f58ce57bf783b4fdf8e957a881f84a8f434cf5ff94d7ac13a00c92` 和 `990c361f73fe9760a2644d7a139a3e4fe2377f22a054298f87883656b3332533`，与当前仓库的 0.6.0 图形资产一致。
- 运行时追踪发现 Unity 在 `GL_COPY_WRITE_BUFFER` 使用 `0x36` 映射标志，连续切换不同缓冲区时大量得到空指针；一轮记录中有 8,662 对调用、4,304 次空返回。追踪未覆盖 unmap，不能逐次证明应用的完整调用状态。独立 GLES 探针在同一宿主的原生 Adreno 驱动通过，而 Guest GPU 桥对同目标多对象、跨目标同对象、删除映射对象和共享上下文场景失败；名称重用未发生，该子项跳过。
- 使用与现有资产逐字一致的历史相机实验源码构建临时 Guest 桥诊断库，仅在安兔兔 Unity 和定向探针的 COPY_WRITE 写入及失效路径测试按对象映射。该探针的 `0x36` 显式 flush/读回由失败变为通过；安兔兔两项 GLES 3.0 场景均显示画面并返回帧率。诊断库不正确覆盖共享上下文、对象重用、跨目标及完整错误语义，不能发布或视为正式修复。
- 实验前将实例运行副本中 EMU 自带 GLES 库备份并核对摘要，实验后已恢复，文件摘要及权限/安全标签与原值一致；原始 ROM、Guest 应用和 APK 未修改。后续修复范围限定为 EMU 端源码和 APK。测试入口为 `tools/diagnostics/gles-buffer-map/`；该映射问题的正式修复和回归尚未完成。
- `tools/diagnostics/gles-bridge-local-test/` 新增仅用内存替身、不连接设备或 socket 的生产 GLES 编码器回归。独立复验当前六项均失败：同目标两对象、跨目标、共享上下文、销毁首个共享上下文、显式 flush、删除后名称复用；脚本按实际失败返回非零。另以 WSL Mesa 验证共享 context 中旧对象仍可绑定、同名新对象可再生成；简单按数字名称清理绑定会误伤旧对象，故本轮尝试的一行改动已撤回，本地 GLES 资产恢复为此前 `6a609982e6402da3be0f04f5381c34d6a32cf5093b43e093c08b022c0306072d`。这只是编码器与软件驱动对照，不证明真实宿主桥或安兔兔已修复。
- 修复 EMU GLES 桥的另一项同名对象状态错误：共享上下文删除并重用 buffer 名称后，`glBindBuffer` 不再因数字名称相同而跳过实际绑定；ARRAY_BUFFER 绑定后失效顶点属性去重缓存，避免再次设置相同属性时沿用旧对象。`tools/diagnostics/gles-buffer-rebind/` 的五项生产编码器回归及 Mesa 同名缓冲和浮点属性绘制对照独立通过，修复前独立对照失败。该变更增加重复绑定及随后的属性设置命令，实际帧率影响未测；不修复 map 按目标跟踪，后者现有回归仍失败，不能称安兔兔黑屏已解决。
- 用现有相机兼容构建脚本重建 API 36 GLES 资产，独立重建摘要一致：宿主桥仍为 `990c361f73fe9760a2644d7a139a3e4fe2377f22a054298f87883656b3332533`，新资产为 `130c61bc1b24e3b9cc72222064a57d8a4719ae385470556f37281e545b59f874`，AArch64、16 KiB 加载段对齐。替换前资产 `6a609982e6402da3be0f04f5381c34d6a32cf5093b43e093c08b022c0306072d` 备份在 `%TEMP%/rme-gles-rebind-before-8m0dfxv7/libGLES_aemu.so`；回滚需核对摘要并仅恢复本轮两处绑定/属性缓存源码与对应资产，不回退已有像素存储修复。此项尚未安装或在设备运行。
- 从当前 HEAD 的隔离快照叠加本轮图形源码与资产，Windows 离线 `:app:assembleDebug` 成功；Debug APK 位于 `E:/RedMagicEMU/local-data/rme-root-archive-20261117/rme-gles-rebind-apk-0h9xdjg6/app/build/outputs/apk/debug/app-debug.apk`，SHA-256 `f623c98dd0f3ef4f6ae2b6ce4b11053267e0c3ac216400b31a97d42299be97fe`，独立核验 APK 内 GLES 资产与上述摘要一致。Windows 单元测试 722 项、1 项失败、89 项跳过；失败为此前记录的 `InstanceRuntimeLeaseTest.anotherProcessBlocksAndDeathReleasesLock`。该 Debug 包版本元数据仍为 0.6.0-beta/code 11，未安装或验证覆盖现有安装；map 错误、安兔兔黑屏和 VK/CL 仍未解决，不作为这些功能的修复包。
- 为恢复安兔兔 Unity 场景显示，将已验证能出画面的映射实验接入 EMU。仅对 `com.antutu.benchmark.full.lite:unity` 的 `GL_COPY_WRITE_BUFFER`、访问标志 `0x36` 生效，按共享组保留映射内存并在对象身份有歧义时拒绝操作。生产编码器电脑回归与 ASan 各 8 项通过；通用 map 回归 15 项仍有 14 项失败。宿主写入无应答，跨目标、跨 context 和完整错误语义未实现，退休映射最多占用 8 个槽直到共享组销毁，不能视为通用 GLES 修复。详情见 `tools/diagnostics/gles-unity-map-experiment/README.md`。
- 从冻结的 `guest.c`（SHA-256 `f8dba1fe15e09b462db799224711fdba7de0294b2101c7027153a2375be90a72`）隔离重建实验 GLES 资产，SHA-256 `f0fbecaa7f7590992d53c9c36afebce60f5506e71674fd837007430706226db9`；相机宿主库仍为 `990c361f73fe9760a2644d7a139a3e4fe2377f22a054298f87883656b3332533`。替换前 GLES 资产（SHA-256 `130c61bc1b24e3b9cc72222064a57d8a4719ae385470556f37281e545b59f874`）保存在 `%TEMP%/rme-unity-before-17c88150fb1643d5b95cacc89642af7f/libGLES_aemu.so`；回滚时核对摘要后恢复该文件并撤销对应 Unity 实验源码，保留原有像素存储及缓冲绑定修正。
- 实验 Debug APK `E:/RedMagicEMU/local-data/rme-root-archive-20261117/rme-unity-final-gxqpe5t4/app/build/outputs/apk/debug/app-debug.apk`，SHA-256 `34bcd35747fdc99b44001af773217f3e0a3714310903e637903f8083f31ed370`；APK 内资产与上述摘要一致，离线构建成功。Windows 全量单测 722 项、1 项已知文件锁失败、89 项跳过。经核对现有 EMU 签名一致后覆盖安装成功，保留应用数据；原始 ROM 未修改。安兔兔实机结果待验证，VK/CL 仍未接入。
- 上述首个隔离包启动既有实例时报 `Conflicting radio declaration`。核对发现 Windows `git archive` 将 radio 清单由 LF 转为 CRLF，导致资产与 0.5 APK 中的 LF 字节不一致；没有修改或删除实例中的声明，也没有关闭校验。改用工作区原始引擎资产字节重新隔离构建，205 个索引条目均校验；修正后实验 APK `E:/RedMagicEMU/local-data/rme-root-archive-20261117/rme-unity-radio-lf-c7umzq2w/app/build/outputs/apk/debug/app-debug.apk`，SHA-256 `caac8304ca8e6f468365506e402688eba7571e0bc7ebb60cb54ef7fe1490fa6f`，radio 资产与 0.5 包一致，GLES 资产仍为 `f0fbecaa7f7590992d53c9c36afebce60f5506e71674fd837007430706226db9`。保留现有网络闸门和声明检查，radio 启动 8 项及网络闸门 1 项定向测试通过；已覆盖安装并开始重试，最终实机结果待验证。
- 修正后的实验包越过声明冲突并进入原有实例。用户提示按 Guest 电源键后画面恢复；重新运行安兔兔 Lite 12.0.3 时，第一个 GLES 场景能出画面，但随后返回原有结果页。只读检查的相关崩溃记录显示 Unity 进程两次发生空地址 `SIGSEGV`，栈顶为 `__memset_aarch64`；原结果页帧率未变化，不能作为本次成绩。失败原因尚未定位，安兔兔测试未通过。
- 为定位空指针，对 Unity 实验路径增加有次数限制的 `unity_map_reject` 日志，覆盖 16 类本地失败原因；生产编码器日志定向测试及原 8 项电脑回归通过。基于源码 SHA-256 `bcf90765d7a54525a3c46c06816d21adf3d8c9922a81a942b5c6c41e7fcbf70f` 重建的 GLES 资产 SHA-256 `10d878fc4c3fc3a5e012b0df4f2365db9dcdcbd16cd77172426892c6575202e1`；替换前 `f0fbecaa7f7590992d53c9c36afebce60f5506e71674fd837007430706226db9` 保存在 `%TEMP%/rme-unity-diag-before-ec2a7c210ca94cd8a005bfef1fffe1ba/libGLES_aemu.so`。诊断 APK `E:/RedMagicEMU/local-data/rme-root-archive-20261117/rme-unity-diag-iwo73fef/app/build/outputs/apk/debug/app-debug.apk` SHA-256 `78165e37e7ea008110499620ddeed606aecca406e6d830ee9de0819d0f122ac7`；保留正确的 radio LF 字节与网络闸门，已覆盖安装。诊断只报告编码器已知的拒绝，不能验证宿主实际写入；本身尚未修复闪退。
- 用户启动诊断包复测后，Unity 再次闪退；EMU 日志记录 `unity_map_reject reason=unsupported_flags_busy`，所请求的映射标志为 `0x6`（WRITE | INVALIDATE_RANGE），说明同一共享组存在 `0x36` 映射时实验实现会拒绝应用实际使用的另一种合法路径。后续需实现该路径并用解除映射后的字节写回及实机结果验证，不将旧成绩当成当前通过。
- 将 `0x6` 加入 Unity 实验镜像路径，并在 unmap 时写回完整映射区间；`0x36` 仍按显式 flush 写入。新增双对象及两种标志混用、非零偏移写回的生产编码器测试，修复前失败；修复后 10 项普通及 ASan 回归、16 项日志回归通过。源码 SHA-256 `c76a44a0f04c0ecddd511fcc130544a7c4b755b8e9552546ac22afb1c5a812b6`；新 GLES 资产 `dbc3fcf0d9274bd89199152cb779d4244df1278b8feecbce5695d198ebb18909`，替换前诊断资产备份于 `%TEMP%/rme-unity-map06-before-bef2c9463cbe483c9dffb8fdf4ec1f2e/libGLES_aemu.so`。隔离 APK `E:/RedMagicEMU/local-data/rme-root-archive-20261117/rme-unity-map06-8hh8t403/app/build/outputs/apk/debug/app-debug.apk`，SHA-256 `3370359ced13414bdbb699d44bfbedcf909bccc3ab1a768012beb26136b559f5`，保留正确的 radio LF 与网络闸门，已覆盖安装。
- 实机在该包下运行安兔兔 Lite 12.0.3：3D 场景正常进入，完整测试结束后结果页显示更新的两项帧率。电脑 OCR 读取孤立场景屏幕/离屏约 `66.93/73.72131` fps、海岸线约 `62.43/139.9359` fps，OCR 数字尚未由人目视复核。现有 Unity 日志无本次空指针异常及 `unity_map_reject`；这验证本次针对该测试的运行。
- 已在电脑用 NDK 28.2 运行 `python tools/diagnostics/oes-fbo-prototype/build.py --ndk <NDK目录>`，输出的两份未 strip 库与上述现有资产摘要均一致；该脚本先对仓库桥源码应用相机实验变更再构建。独立 GLES 探针在 WSL Mesa llvmpipe 通过全部场景，包括实际发生的名称重用；这是原生 GLES 对照，不是 EMU 桥修复后的结果。
- `tools/diagnostics/gles-map-protocol-research/` 是仅供电脑运行的独立协议研究，不接入产品。Mesa/socket 数据传输与绘制对照通过；对象地址重用、并发失效、生命周期和错误转发回归失败，ASan 还在实验代码复现悬空写入。统一脚本对安全失败返回非零；在这些问题解决前不得将原型合入 EMU 桥或替换资产。

#### 0.6.1 像素存储参数与计算接口调查（2026-11-16）

- EMU 的 GLES 编码器先前会把无效的对齐值、负行长度或负跳过量写入本地像素布局状态，即使宿主 GL 会拒绝它们。定向测试在旧源码上复现 3×2 RGB8 传输长度误算为 18 而非 21 字节；现在只在参数有效时更新本地状态，仍把原始调用发给宿主，由宿主报告 GL 错误。`test_pixel_store.py` 在修复前失败、修复后通过；图形读回和 YV12 现有电脑回归通过。此项不证明安兔兔映射问题已修复，也未处理扩展不支持、超大尺寸或设备行为。
- 用 NDK 28.2 和 `tools/diagnostics/oes-fbo-prototype/build.py --ndk <NDK目录>` 重建保留相机转换的 EMU 库：宿主库仍为 `990c361f73fe9760a2644d7a139a3e4fe2377f22a054298f87883656b3332533`，更新后的 API 36 GLES 资产为 `6a609982e6402da3be0f04f5381c34d6a32cf5093b43e093c08b022c0306072d`；重复重建摘要一致。替换前资产 `6cc7f15ce1f58ce57bf783b4fdf8e957a881f84a8f434cf5ff94d7ac13a00c92` 备份于 `%TEMP%/rme-gles-pixel-store-before-1302vkcv/libGLES_aemu.so`；回滚时核对摘要、恢复该资产及本轮 `guest.c` 改动后重建。原始 ROM、设备 Guest 和已安装 APK 未改。
- Windows JDK 21 独立输出目录 Debug APK 构建成功，APK SHA-256 `3add12d66383a1123573a7e2c95e446966ba7af2d6281a64428f37f7a8d9c3b9`；检查 APK 索引和 blob 中的 GLES 资产与新资产一致。完整单元测试 722 项中 1 项失败、89 项跳过；失败为先前记录的 Windows 跨进程文件锁测试 `InstanceRuntimeLeaseTest.anotherProcessBlocksAndDeathReleasesLock`，子进程退出后仍报锁占用。常规工作区增量构建此前另在 `compressDebugAssets` 因已有 `engines.idx.jar` 冲突失败，未清理用户既有构建产物。未安装或实机验证 APK。
- 从当前 HEAD 创建 WSL 隔离源码快照，并叠加本轮桥源码、测试和资产；使用 JDK 21 及项目 SDK 缓存离线执行 `:app:testDebugUnitTest :app:assembleDebug`，722 项测试、0 失败、0 错误、3 跳过，构建成功。快照中的 GLES 资产与工作区逐字一致；WSL Debug APK SHA-256 `beae0bce4d4a2597d74618dba7cc3afa800f77505320659ae114dbfe1a7c98a3`。快照不包含未跟踪的其他研究目录，未安装或在设备运行。
- Vulkan/OpenCL 源码调查：当前 N64 为 GLES 桥/软件 GLES，未发现 EMU 自带 Vulkan 或 OpenCL 计算桥。Android 的 Vulkan loader 仍需实际驱动；[Geekbench 6 官方文档](https://www.geekbench.com/doc/geekbench6-benchmark-internals.pdf)列出的 GPU Compute 最低版本为 Vulkan 1.2 和 OpenCL 1.2。后续应优先验证 EMU 用户态 Vulkan compute 到宿主公开 API 的设备、句柄、内存及同步链，再评估 OpenCL 到 Vulkan 的实现和运行时内核编译；不得直通真实硬件节点、伪造能力或把 CPU 软件 Vulkan 结果称为 GPU 加速。两者均未实现或设备验证。

#### 0.6.1 Vulkan 计算的电脑端基线（2026-11-16）

- 新增 `tools/diagnostics/vulkan-compute/` 独立探针；使用 Vulkan 1.2 和自带计算着色器，对 64 个整数执行变换并逐项核对。探针按驱动报告类型优先选择物理 GPU，并展示实际设备、API 及内存属性；无宿主型号硬编码。它不接入 EMU 或 Guest。
- 本机 NDK 28.2 生成并校验 Vulkan 1.2 SPIR-V，同一着色器用于 WSL 与 Android ARM64 构建。WSL 的 llvmpipe CPU 软件 Vulkan 1.4.335 完成 64 项结果校验；错误预期和模拟 fence 超时均按预期返回失败。10 秒 fence 超时无法覆盖驱动内部所有调用卡死。
- Android ARM64/API 29 交叉编译成功，ELF 为 AArch64、`/system/bin/linker64`，LOAD 对齐 16 KiB；生成文件 SHA-256 `549bfbe77089c3cf51daef64614a0f83491d750ed440653e71d68bde731ca2b7`，保存在忽略的 `build/diagnostics/vulkan-compute/`。用户报告宿主为骁龙 8 Gen 3；本轮未访问手机，未验证其实际驱动、Adreno 执行或 Geekbench。OpenCL、EMU Vulkan 桥及既有 GLES 缓冲区映射问题仍未修复。

#### 0.6.1 Vulkan 桥握手的电脑端验证（2026-11-16）

- 新增独立的 `native/vulkanbridge/` 握手协议：固定长度、小端序帧交换协议版本，区分版本拒绝、无效帧及连接错误；收发共享单一单调时钟期限，调用方保留并独占 socket。它尚未接入 EMU，也没有启动服务、实现 Vulkan 命令或提供 GPU 能力。
- WSL 使用真实 socketpair 验证版本匹配与拒绝、分片和短写、错误帧、断连、超时及中断重试；AddressSanitizer/UndefinedBehaviorSanitizer 同样通过。对临时源码副本进行的错误注入确认短写、中断重试和总期限测试能检出相应错误。使用 NDK 28.2 交叉编译并链接 Android ARM64/API 29 测试程序成功，ELF 使用 `/system/bin/linker64` 且 LOAD 对齐为 16 KiB；未在 Android 上运行。
- 后续接入仍需连接状态、实例隔离、授权、资源限制和实际 Vulkan 后端；OpenCL、EMU Vulkan 计算和 GLES 缓冲区映射问题仍未完成。本轮未访问手机、安装 APK 或更改设备 Guest。

#### 0.6.1 Vulkan 现成传输路线的电脑端调查（2026-11-16）

- 当前 N64 无 Vulkan HAL 或宿主 Vulkan 解码器。AOSP gfxstream 固定版本 `android-platform-15.0.0_r20`（提交 `d7a33801fb557cebd4f8564a8af431669b3f471d`）包含完整的 Vulkan 编解码，但其测试传输的共享地址空间和模拟 fence 依赖同进程，不能直接改成 socket 即用于 EMU；还需实现跨进程内存、同步、生命周期和错误处理。源码归档在临时目录检查，未引入仓库；实际引入时须逐项保留并核对其多种许可证。
- 同一固定 gfxstream 版本和同标签 AOSP aemu，在 WSL 隔离目录成功编译 x86-64 Linux Vulkan-only host backend；使用 GCC 15 时通过临时编译器包装补充 `cstdint`。构建日志位于 `/var/tmp/rme-gfxstream-build.hTnuwsWV/host-build3.log`；它依赖隔离目录内的 aemu 库，尚无配套客户端、跨进程资源传输或端到端运行，也不是 Android 宿主库。
- 随后在同一隔离目录编译固定版 Rutabaga FFI 和 gfxstream guest-test ICD，连接同进程测试后端与 llvmpipe。该链路经过若干临时兼容和诊断补丁（逐项差异保存在 `audit/`），成功执行并核对 64 项 Vulkan 计算，正确释放正常路径资源且退出 0；错误结果对照退出 1。独立复验确认宿主实际为 llvmpipe CPU，尽管测试客户端将其报告为 virtual GPU。此测试没有经过 EMU 的进程边界或 Android 驱动，也未证明设备加速。
- 失败路径仍不安全：模拟 fence 等待失败后没有成功标志，但进程退出时出现 SIGSEGV；独立复验得到同样结果。另一个仅在真实 fence 已成功后注入假失败的对照能正常关闭并返回 1。固定版还有描述符数组和命令缓冲数组 `reserve` 后越界写入，以及对非 buffer 描述符读取应忽略字段等缺陷；临时补丁只用于推进单项实验，不能作为完整修复或接入产品的依据。
- 保存实验补丁、关键日志、测试源码和上游许可证到 Git 忽略的 `build/diagnostics/vulkan-compute/gfxstream-evidence-d7a33801.tar.gz`，SHA-256 `dead6c1261efb1cdbeea5df27eb646babb607dbd6f40722cd2a12728fb32b97f`；该证据包不含完整源码和依赖，脚本仍依赖原临时目录，并不是独立可复现的构建包。
- Android ARM64/API 29 的 aemu 四个依赖已在隔离 wrapper 中交叉编译，使用共享 libc++、16 KiB LOAD 对齐，并显式修正 snapshot 到 base 的链接和使用固定 LZ4。gfxstream Vulkan-only Android 目标完成 82/83 步，最终缺少 GPU 信息收集和 Vulkan 安全判定两个符号；固定版 Android.bp 使用的 Linux 实现不采集信息且恒定放行 Vulkan，本实验未采用，也未关闭未定义符号检查。Android 后端仍未链接成功，日志在 `/var/tmp/rme-gfxstream-android-config.cj2YQmgw/build.log`。
- 为研究真正跨进程的现成传输，另在隔离目录固定 gfxstream `cf47bd2660e7217f5729c91664d36d81c1b3282a` 及其源码列出的 Rutabaga `9f22cc994019486a03086992609c0aa4769ae4b0`、Mesa `6e4a77089e1db6cb6269ec694de3a1a131f96689`；本机服务端和 Kumquat 客户端均完成构建。使用私有挂载命名空间隔离测试 socket，客户端经 socket 成功枚举 llvmpipe，尚未完成计算。默认模式的宿主 ColorBuffer 初始化失败；上游的 HostAllocation 软件模式可启动，但跨进程 `vkAllocateMemory` 返回 `-2`。GDB 确认请求到达宿主 Vulkan 驱动；实验不证明手机表现，也不能作为 EMU 产品实现。完整隔离源码、临时依赖锁文件和日志位于 `/var/tmp/rme-kumquat-research.r6Wg21lO`，未同步项目上游。
- 针对上述实验实际的 storage buffer 和 RGBA8 image 分别查询外部内存属性：该 llvmpipe 对 OPAQUE_FD、HOST_ALLOCATION、DMA_BUF 只分别报告自身兼容，且后两者未报告可导出；gfxstream 分别请求了 `0x280` 和 `0x201` 的多类型导出组合。能力保护探针按这两个实际参数拒绝这些组合；单独 OPAQUE_FD 的正对照仅说明该 buffer 类型可导出，不证明其可直接 `mmap` 或可供跨进程通信。驱动能力和后续回退方案需在真实设备上另行验证。
- `tools/diagnostics/vulkan-external-memory/` 新增只查询、不分配的外部内存探针，分别报告指定 storage buffer 与 RGBA8 image 的单个 handle 及 `0x201`、`0x280` 多 handle 可导出兼容性；测试覆盖缺失查询、不可导出与不兼容等边界。WSL llvmpipe 查询、无驱动负对照和 ARM64/API 29 交叉编译已独立验证；Android 探针保存于 `build/diagnostics/vulkan-compute/external-memory-probe-arm64-api29-9b564d3e`，SHA-256 `9b564d3e964514ad6d77493fa123ab241de92546e4a74e74fa4acac9323a8be9`，未在手机运行。查询使用的 image 参数与前述初始化参数不同，不应直接互相替代。
- 后续启用上游 `VulkanExternalMemoryMode:HostAllocation,SystemBlob:enabled` 配置，在私有命名空间重新测试：服务端和客户端为不同进程，三轮 Vulkan 计算各校验 64 项通过；无服务端及错误预期对照均返回失败。独立复验也通过，跟踪显示服务端创建共享内存、通过 Unix socket 的 SCM_RIGHTS 传递 FD、客户端以 MAP_SHARED 映射。宿主依然是 llvmpipe CPU；服务端由测试发送 SIGTERM 结束，未验证优雅关闭。上游警告 SystemBlob 在非 Windows 平台未经充分测试，挂起/恢复仍有 TODO；这是电脑端调查结果。
- 对上述固定 gfxstream 发现独立错误：没有 GL backing 时 `updateGlFromBytes` 返回成功。`tools/diagnostics/gfxstream-color-buffer/` 保存上游固定提交的最小研究补丁与回归；同一真实库测试在原版退出 1、修正版退出 0，无须同步路径行为保持不变。修正版重跑三轮跨进程 CPU Vulkan 计算通过。补丁未接入 EMU，真实 GLES backing、错误处理和生命周期尚未覆盖。
- 保存固定版本、临时 Cargo 锁文件、能力查询、独立复验日志及上游许可证说明到 Git 忽略的 `build/diagnostics/vulkan-compute/kumquat-evidence-cf47bd26.tar.gz`，SHA-256 `08abed507b3593f0a5cc88cd098888d8f60f639b5c767f9b8d9f337b2e8907ca`；证据包不包含完整源码与构建依赖，不能单独重建。
- Android 移植继续验证：Kumquat Rust 传输 FFI 已在隔离目录编译并严格链接为 ARM64，但完整 Mesa Vulkan HAL 最终链接缺 Android 16 的 `AHardwareBuffer_getDataSpace` LLNDK/system API，未以假符号或关闭检查绕过；API 31 分支的“未知”色彩空间在上游会被转成具体建议，不能当作无损回退。上游客户端还会宣告 Android 硬件缓冲扩展，尚未证明可以安全限定为不涉及 AHB 的纯计算。此项仅为客户端交叉构建调查，不改变宿主 APK 的 API 26 最低版本，也未改动设备 Guest。
- Kumquat 的后续负向测试显示，仅启用 Vulkan capset 时仍可查询并创建未列出的 GLES context；断线后 context、资源的全局归属和共享凭据也未正确隔离。`tools/diagnostics/kumquat-eof/` 保存针对固定版本的独立 EOF 补丁与真实 socket 回归：原版 FD 随 30 次断线由 8 增至 38，修正版保持 8；10 次各排队两条消息均收到响应，已有和新建 Vulkan 客户端仍完成计算。测试进程组超时清理的失败与修复对照也已独立验证。该补丁只解决连接 FD，不解决 context、resource 与 fence 生命周期；均未接入产品。
- `tools/diagnostics/kumquat-capset/` 单独保存未启用 capset 的连接级拒绝研究补丁。真实 socket 独立复验：原版对三个未启用/未知查询和两个 context 创建返回成功；修正版只关闭相应请求连接，合法 Vulkan context 及已有/新建客户端计算通过。该补丁不包含 EOF 修复，也不解决跨连接资源归属或断线回收；旧式默认 capset 回退的兼容性仍未评估，未接入产品。
- EOF 与 capset 修改同一事件循环，已在隔离源码中合并并分别重跑两套真实 socket 回归：FD 数稳定、排队消息 10/10 完整，五项未启用请求各自关闭连接，已有及新建客户端计算均通过。合并研究补丁另存 `tools/diagnostics/kumquat-capset/combined-eof-capset.patch`，SHA-256 `83e8de9a51b34e392cd088a3b54d257cfb121ebc70e72767cb5b26b6d504bf2b`；不能与两个单项补丁重复应用，资源归属与生命周期仍未解决，未接入产品。
- 固定 Mesa gfxstream 客户端为测试强制通告 `VK_EXT_device_memory_report`，但部分失败与宿主内部内存事件尚未验证完整。`tools/diagnostics/gfxstream-memory-report/` 保存独立研究补丁，实验中同时隐藏扩展并设置 feature=false；真实 Vulkan 复验显示原版扩展请求成功、修正版返回 `VK_ERROR_EXTENSION_NOT_PRESENT`，其余已比对能力一致，两版普通计算和错误结果对照均符合预期。测试服务异常退出、强杀和有界清理另做了独立回归。补丁尚未接入产品。
- 另一候选 Mesa Venus 使用现有 WSL Mesa 26.0.8 virtio ICD，Ubuntu `virgl-server`/`libvirglrenderer1` 1.2.0-2ubuntu2 及依赖只在临时目录解包，用私有 socket、llvmpipe 宿主和同一 Vulkan 计算探针测试。直接 llvmpipe 64 项计算通过；经过 Venus/vtest 的客户端超时，未完成计算。首次为缺失渲染进程路径，修正临时进程路径后进入资源创建；跟踪发现 Vulkan 资源成功导出为 opaque fd，而该 vtest 只接受 DMA-BUF 或 SHM，返回 `-EINVAL`。不能将 opaque fd 当成可直接映射的 fd。
- Android OpenCL 入口还涉及应用动态链接命名空间及运行时内核编译器；库能否加载取决于应用的链接命名空间。上述两条 Vulkan 路线均未接入 EMU，OpenCL 尚未接入；本轮未访问设备或改动固件。

#### 0.6.1 OpenCL-on-Vulkan 的电脑端基线（2026-11-16）

- 在 WSL 独立目录构建 clvk `a56c4a442a4671fc08478ab39463a1da28e4a818`、clspv `545a65b5f4fda9a7c285bb7fd75eda86aa2d853b` 和 LLVM `c105848fd29d3b46eeb794bb6b10dad04f903b09`。clvk 和 clspv 分别选用上游列出的认证标签，但 clvk 提交中的 clspv 子模块原指向 `c8c27e92503d8224a1b1f10e8a4491165222f20c`，本实验显式切换为前述 clspv 标签；本实验未执行认证。子模块版本、工具包及构建配置记录于 `/var/tmp/rme-clvk-build-dEVbhfMR/MANIFEST.txt`。GCC 15 兼容处理为构建参数补 `cstdint`，并只对一个对象添加 `rsqrt` 兼容头；CMake 重新生成会覆盖后者，不能将此临时构建当作可复现的正式构建方案。固定源码保留原始许可证，未引入仓库。
- 使用 llvmpipe CPU 软件 Vulkan，clvk 上游 `simple_test` 从 OpenCL C 源码运行时编译并执行 kernel，逐项校验 256 个整数，通过；把 clspv 路径改为不存在的文件且禁用缓存后，`clBuildProgram` 按预期返回 `-11`。成功与失败对照均已独立重跑，实验目录保留运行日志。这验证的是电脑软件计算，不是手机 GPU、Geekbench 或 EMU 内 OpenCL。
- 新增 `tools/diagnostics/opencl-vulkan/` 可重复运行的隔离诊断，显式输入已构建的测试、库、编译器及 ICD，不自动获取或安装依赖；9 项失败处理回归和上述真实 llvmpipe 正负对照通过。该工具信任调用方提供的二进制，不能独立证明其来源或实际设备类型。
- 固定 clvk/clspv 与前述 Mesa gfxstream/Kumquat 组合，在私有 socket 上独立复验完整 OpenCL 计算链：OpenCL C 源码经 clspv 编译、Vulkan 经独立客户端与服务端执行并校验 64 项结果。无服务端、无效内核及故意错误结果均失败；调用跟踪确认编译器及 socket 收发。客户端将虚拟设备报告为 GPU，但服务端明确选择 llvmpipe CPU。服务端 SIGTERM 退出并非优雅关闭；SystemBlob 的非 Windows 限制、隔离和 Android 接线均未解决。证据保存在 Git 忽略的 `build/diagnostics/opencl-vulkan/clvk-kumquat-evidence-cf47bd26.tar.gz`，SHA-256 `1049ac622a282a74c67466d6e2c3a4e2536825ca8cab1bbb15671b290abd7481`；证据包不是完整构建包。
- 闭环复验脚本 `tools/diagnostics/opencl-vulkan/run-kumquat.sh` 显式接收已有组件路径，不自动下载，并使用隔离命名空间和有界进程清理；从工作区脚本独立重跑后，三项负对照与 64 项计算均符合预期。该脚本依赖的上游实验构建仍位于临时目录，未打包进 EMU。
- 新增独立 `probe.c`：报告 OpenCL 设备类型、在线编译内核并校验 64 项结果。WSL llvmpipe CPU 正常计算通过，错误预期、无效内核按预期失败，UBSan 验证通过；设备类型由 OpenCL 实现报告。Android ARM64/API 29 探针已交叉编译、16 KiB 对齐，保存在 `build/diagnostics/opencl-vulkan/opencl-probe-arm64-api29-13b4f88b`，SHA-256 `13b4f88b898770ce294d3e99aed3e4be9402f697ced9a4e6451e2126e10631e9`，尚未在设备运行。
- 使用通过官方 SHA-1 校验的 Linux NDK r28c，在隔离目录交叉编译 Android ARM64/API 29 clvk，启用同进程 clspv；固定 SPIR-V IL 转换器与 LLVM 主版本不一致，本次关闭 IL 路径。精简后的试验库 `build/diagnostics/opencl-vulkan/libOpenCL-arm64-api29-experimental-1444f8da.so` 为 64,359,952 字节，SHA-256 `1444f8da141eb995744ad877434a70cb1433115e24d645921a0544a0e2cf8f55`；原始库及配置日志留在 `/var/tmp/rme-clvk-android-noil.rxik0nVG`。ELF 为 AArch64，SONAME `libOpenCL.so`，LOAD 对齐 16 KiB，主要 OpenCL 接口有导出；对 NDK API 29 的依赖符号做了静态核对。上游源码和多种许可证未打包入此试验文件，未经许可证审查，不得作为发行资产；更未证明目标固件动态加载或实际运行。该库尚未接入 EMU 或安装，设备和原厂 ROM 均未操作。
- OpenCL 探针新增工作组归约：4 组各 16 项经 `__local` 数组及 barrier 计算，逐组核对。独立复验中，原生 clvk/llvmpipe 与跨进程 clvk/gfxstream/Kumquat 均计算正确，故意错误结果对照返回失败；前者另经 UBSan 验证，后者原有负对照也通过。两条路径均为电脑软件 CPU。
- 固定 clvk 对该内核的 `CL_KERNEL_LOCAL_MEM_SIZE` 报告为 0，其 `src/kernel.cpp` 明确有漏算编译期 local 分配的 FIXME；工作组计算和容量查询分别记录。新版 ARM64/API 29 探针仅交叉编译，16 KiB 对齐，保存在 `build/diagnostics/opencl-vulkan/opencl-probe-arm64-api29-2c3ece5a`，SHA-256 `2c3ece5a64f105cbc47dd43e020e68ea97bf2cc14abac819339d2a53757dfb17`；旧版探针继续保留，均未在手机运行。
- Vulkan 握手原型增加超时边界检查：`poll` 返回就绪后，如果检查时已超过整体期限则返回超时。模拟延迟恢复的 socket 回归在修改前复现错误成功、修改后通过；WSL 测试独立复跑通过，ARM64/API 29 编译通过。该原型仍未接入 EMU，不构成 Vulkan 计算实现，也不能保证线程调度导致的所有超时边界。
- `tools/diagnostics/clvk-local-memory-report/` 保留静态与动态 local 内存查询的独立失败回归。固定 clvk 在 llvmpipe 上报告 0/96 字节，而该测试下限分别为 64/160 字节；`uint3[7]` 下限为 112 字节但报告 0，独立复跑均复现。多 kernel 的入口 interface 会保守列出其他 kernel 的变量，动态 local 还涉及特化常量；按模块或 interface 简单相加并不正确。此缺陷尚无通用修复，未接入产品。
- Android Vulkan 客户端进一步在隔离目录使用 Android 16 官方 Soong `ndkstubgen --llndk`、对应原始 `libnativewindow`/`libsync` 符号表、NDK `liblog` 链接接口和部分 AOSP 静态属性代码，完成严格 `--no-undefined` 交叉链接；独立临时副本复验输出 SHA-256 `277da3a6e1c765d8c8e2577d9bbc56c8bd633a8194a4e2daebfa047245186e0f`，无绝对路径 DT_NEEDED 或 RUNPATH。链接期 `libhardware` 仍使用 Mesa 构建 stub，其他依赖、ABI、目标系统真实符号和加载命名空间均未完整验证；该 ELF 与生成的链接 stub 只用于诊断，未装载或接入 EMU，也不得作为发行资产。源码、构建工具与复验脚本位于 `/var/tmp/rme-llndk-android16.s64s0A7i/`，非完整可独立重建包。
- 后续以 AOSP 官方 VNDK 34 ARM64 `libhardware.so` 替代上述 Mesa 链接 stub，同样完成严格链接，独立复验得到逐字相同的产物；最终 ELF 仍动态依赖 `libhardware.so`。该预构建属于 VNDK 34；只用于电脑端链接对照，未运行或打包。官方文件及复验脚本位于 `/var/tmp/rme-libhardware-aosp.OrdwY5up/`，原始来源与许可证见该目录材料，整体发行许可证尚未审核。
- 旧版 gfxstream 同进程实验的失败退出崩溃进一步定位：故障注入使队列状态按未知处理时，静态 `BoxedHandleManager` 先于 `FrameBuffer` 析构，后者的 Vulkan 实例清理回调再访问已析构管理器，触发 SIGSEGV。另一对照中真实 fence 已完成且明确按完成状态显式释放，进程按预期返回失败而非崩溃。这些对照均非真实在途 GPU 超时；真实超时后的队列和线程能否安全收敛仍未知，因此没有通过强行改变析构顺序或跳过析构掩盖该问题。日志与 GDB 证据位于 `/var/tmp/rme-fence-analysis.afzYLKm9/`，未接入产品。
- 为调查静态 local 内存报告，上游较新 clvk 固定提交 `9862fce7d1b9b68a45986ef5e15b2fb81e417246` 增加了编译器反射路径，配套 clspv 为其 gitlink `b36e501783d336e8ba2fd26c6832d6d6faf74d5d`；截至本记录仍在隔离构建，尚无可运行回归结果。新版 `device.cpp` 用当前 NDK r28c 自带 Vulkan 1.3.275 头进行 Android ARM64 编译失败；仅替换为固定 gfxstream 源码自带的 Vulkan 1.4.363 头后，该单个对象交叉编译通过，独立复验命令与日志保存在 `/var/tmp/rme-clvk-header-verify.UI5KDEA2/`。源码对新增 FMA 扩展有运行时枚举与 feature 检查；这不证明 Android 整库链接、目标驱动支持或运行成功，也未接入 EMU。
- 上述新版固定组合后来在 WSL 隔离目录构建完成并独立复验：旧版失败的静态及动态 local 内存查询下限测试通过，报告 176/272/176 字节；实现汇总程序级静态分配。新版同时经 clvk → gfxstream → Kumquat 跨进程链通过逐元素和工作组归约，缺服务器、坏内核及错误预期均按预期失败；宿主是 llvmpipe CPU，测试服务 SIGTERM 强制结束。这是电脑研究构建，未完成新版 Android 整库构建、EMU 接入或手机 GPU 验证；版本、构建和复现见 `tools/diagnostics/clvk-local-memory-report/README.md`。
- `tools/diagnostics/gfxstream-missing-properties2/` 保存固定 gfxstream 的独立研究补丁：当模拟 loader 同时缺少 `vkGetPhysicalDeviceProperties2` 核心名和 KHR 别名时，原版宿主崩溃，补丁版销毁已创建实例并逐层返回初始化失败；对新建源码副本重建后独立复验，正常和只缺 KHR 别名的 CPU 计算对照通过。只缺核心名的异常 loader 还会在后续实例 decoder 路径失败，尚未修复；这些均为电脑故障注入，未接入产品或在 Android 运行。
- 新版 clvk/配套 clspv 后续在隔离目录完成 Android ARM64/API 29 整库交叉链接，启用同进程 OpenCL C 编译器、关闭 SPIR-V IL；使用固定 gfxstream 的 Vulkan 1.4.363 头及上游 `CLSPV_EXTERNAL_LIBCLC_DIR` 复用相同 LLVM 版本的 SPIR-V bitcode。精简调试信息的研究库 `build/diagnostics/opencl-vulkan/libOpenCL-arm64-api29-newclvk-experimental-a3c729d8.so` 为 97,543,200 字节，SHA-256 `a3c729d81a07da65a50213e787569ce0205cea4d16746d09939b5b5ea9554430`，ELF LOAD 对齐 16 KiB；工作组探针以严格未定义符号检查成功链接。独立静态检查中，探针、该库和 NDK C++ 运行库的强未定义符号均在指定库或 NDK API 29 stub 找到候选导出；弱符号、Android 加载命名空间和实际运行未验证。该库未接入 EMU 或 APK，依赖及许可证尚未完整审查，不得作为发行资产。
- 同一研究库与 NDK API 26 接口静态对照，有 `vkGetPhysicalDeviceFeatures2` 和 5 项 `posix_spawn` 强符号找不到候选导出，首次出现在 API 28 stub；产物的实际构建目标是 API 29。所需系统库取决于实际加载该库的进程及其命名空间；比较脚本和日志位于 `/var/tmp/rme-clvk-api-analysis.otdfHOj8/`，未执行 Android 加载。
- `tools/diagnostics/gfxstream-android-window-type/` 保存固定 gfxstream 的单项研究补丁：Android GLES 关闭时改用 NDK EGL 头提供的原生窗口类型。独立复验在原版复现类型断言和真实源码编译失败，补丁版 GLES 开关两种配置的 ARM64 对象编译及 Linux 类型对照通过。只验证单对象编译；Android 被误判为 X11、Vulkan-only 的 GLES 同步引用、窗口有效性和完整宿主后端链接仍未解决，补丁未接入产品。
- 以红魔原厂镜像为对照，只读检查本机 `super.img` 副本的 LP/EROFS 元数据及图形文件。`vendor/build.prop` 声明 REDMAGIC、NX809J、平台 `canoe`；镜像内存在 AArch64 的 `vendor/lib64/hw/vulkan.adreno.so`、`vendor/lib64/libOpenCL.so`、`libOpenCL_adreno.so`、`libadreno_compiler_cl.so`、`libgsl.so`，以及 `system/lib64/libvulkan.so`。原厂 Vulkan 驱动直接依赖 `libgsl.so`、Adreno 工具库和 QTI 显示 mapper 等；OpenCL 加载器及 Adreno 实现中分别有 `libOpenCL_adreno.so`、`libgsl.so` 和动态加载的字符串线索，不能单凭字符串证明运行时调用。上述检查仅读镜像并在内存解析指定 ELF，未导出固件内容、修改镜像或访问手机。
- 镜像 OpenCL 进一步静态复核：原厂 `libOpenCL.so` 导出 196 个 `cl` 前缀符号，包含平台枚举、源码建程序、构建和提交计算等入口；`libOpenCL_adreno.so` 导出 8 个 `cl` 符号，包括 `clIcdGetPlatformIDsKHR`。主库包含 ICD 配置路径及动态加载名称字符串，镜像原始 `system/vendor` 指向 `/vendor`，对应 `vendor/Khronos/OpenCL/vendors` 目录未找到。这些是 ICD 分发及配置路径的线索，不证明实际加载流程或在线编译能力；也不能排除其他配置来源或回退逻辑。
- 只读原厂库配置的定向检查与独立复验：`vendor/etc/public.libraries.txt` 包含 `libOpenCL.so`，`system/etc/public.libraries.txt` 包含 `libvulkan.so`；system 的 `linker.config.pb` 包含 `libvulkan.so` 字符串，但本次未解析其 protobuf 字段。实际实例的链接器配置在运行时生成，故原厂清单只能证明所列库名。仅读取指定配置中的图形条目，未修改镜像或设备。
- 项目当前源码启动的是 EMU 自带 GLES 桥，N64 的 HWUI 仍设为 `skiagl`；检查 API 36 资源与接线未见现成 Vulkan/OpenCL 桥。仅凭源码不能断言已有实例实际保留或加载了哪些原厂库；需要后续通过允许的正常功能测试验证，不能为了实验修改设备 Guest 或真实硬件节点。
- `tools/diagnostics/gfxstream-color-buffer-nogles/` 保存 Vulkan-only 编译研究补丁和对照：5 个同步方法改用现有跨配置 getter，不改原同步返回逻辑。独立复验中 Android/Linux GLES=0 原版对象编译失败、候选通过，Linux GLES=1 对照通过；GLES=1 构建的运行时 Vulkan-only 路径使用 llvmpipe 验证真实图像写入、读取、同步、错误句柄和正常释放。完整 GLES=0 后端仍因其他源文件错误不能链接，GL/Vulkan 双 backing 和 Android 运行未验证；补丁未接入产品。
- 2026-11-16 对手机宿主进行只读能力查询。系统属性报告 Redmi 24117RK2CC、Android 17/API 37、SoC 标识 SM8650；系统 Vulkan 查询报告 Adreno 750、Vulkan API 1.3.128、Qualcomm Adreno 驱动，并声明外部内存、FD、Android 硬件缓冲及外部 fence/semaphore FD 扩展。系统报告的 compute shared memory 为 32768 字节，最大工作组 invocation 为 1024；这些是驱动查询结果。宿主存在 OpenCL 库，不能由文件存在推断应用可加载或可用。设备上的 EMU 为 0.5.0-beta/versionCode 8，并非本工作区修改后的构建；未安装、推送或启动测试程序，也未读取或修改设备 Guest。
- 2026-11-17 在同一手机宿主临时运行 Vulkan 计算探针。驱动报告 Adreno 750、Vulkan 1.3.128，离屏计算 64 项全部逐项校验通过，进程退出 0；故意错误预期的对照检测到 1 项不符并返回失败。外部内存探针对指定 storage buffer 和 RGBA8 图像报告 AHardwareBuffer 可导入导出；DMA-BUF 和 host allocation 扩展未通告，`0x201`/`0x280` 组合结果为 unknown。该探针只查询属性，不创建设备、分配 GPU 内存或传递句柄；不能把 unknown 解释成支持或不支持，也不证明跨进程同步。
- 手机宿主通过 shell 使用原有高通 `libOpenCL.so` 运行源码编译和计算探针：驱动报告 QUALCOMM Adreno 750/GPU，64 项逐元素计算和 4 组各 16 项的 local 内存归约通过；`CL_KERNEL_LOCAL_MEM_SIZE` 报告 64 字节，无效内核返回构建失败，故意错误的预期返回失败。宿主 `vendor/etc/public.libraries.txt` 列有 `libOpenCL.so`，`system/etc/public.libraries.txt` 列有 `libvulkan.so`；此次加载使用手机 shell 的 `/vendor/lib64` 路径。测试只使用宿主临时目录，执行后确认各次临时探针文件已删除；没有安装 APK，也没有读取或修改设备 Guest。上述均非 EMU 内测试。
- 只读比较原厂 NX809J 镜像和此次手机宿主的四个对应图形文件：`libOpenCL.so`、`libOpenCL_adreno.so`、`vulkan.adreno.so` 和 `libgsl.so` 的 SHA-256 均不同。镜像侧通过 EROFS 读取流在内存计算摘要，手机侧只读取已有文件；未复制驱动或修改镜像。摘要差异只说明文件不逐字相同。
- 新增独立 `tools/diagnostics/opencl-vulkan/device-info.c` 能力查询，WSL 正常枚举及缺失驱动负对照通过，ARM64/API 29 交叉编译与 16 KiB 对齐检查通过。手机宿主的临时测试报告 Adreno 750、OpenCL 3.0、OpenCL C 3.0、图像支持、在线编译器及 `cl_khr_fp16`；探针仅查询属性，不创建 context 或计算任务，执行后临时文件已删除。该报告不证明任何应用或 EMU 的 OpenCL 接入已完成。
- 设备信息探针增加定向外部内存扩展报告，WSL 正反对照及 UBSan 构建通过、Android ARM64 交叉编译通过；手机宿主的 Qualcomm OpenCL 报告 `cl_qcom_ext_host_ptr` 和 `cl_qcom_android_native_buffer_host_ptr`，未报告 `cl_khr_external_memory` 或 `cl_khr_external_memory_android_hardware_buffer`。该查询只列驱动通告的扩展，没有进行 OpenCL/native-buffer 导入或跨进程测试，不能推断已实现互通；宿主临时探针文件已删除。
- 2026-11-17 手机宿主使用公开 NDK/Vulkan API 运行独立 AHardwareBuffer BLOB 单进程读写探针：CPU 写入的 64 个整数经 Vulkan 拷贝后逐项校验通过；Vulkan 写入后 CPU 逐项读回通过；故意错误预期检测到 1 项不符并退出 1。ARM64/API 29 二进制独立重建摘要一致（`af298263227563291555da91445b875a0ebb1d2d7cbcc1391d32d282aee5406a`），正常和负向进程退出后均确认宿主临时探针文件已删除。此结果仅覆盖该 BLOB 的单进程 CPU↔Vulkan 数据传输，不证明图像资源、跨进程传输或 EMU 可用；没有操作设备 Guest。
- `tools/diagnostics/vulkan-ahb-export/` 验证相反方向的 Vulkan 分配并导出 AHB BLOB，buffer usage 使用固定候选后端的 INDEX、VERTEX、STORAGE、STORAGE_TEXEL、TRANSFER_SRC 和 TRANSFER_DST 组合。手机宿主确实成功导出 256 字节 BLOB；其报告的 usage `0x10000ff` 不含本探针接受的 CPU read/write 用途，因此 CPU lock 和往返未执行，探针明确返回不支持而非宣称计算通过。电脑上的数据校验、不支持负对照、ARM64 构建和手机测试二进制摘要已独立复验；手机临时文件已删除。只证明该条件下单进程导出成功，不证明后续重导入、其他尺寸、跨进程或 EMU 可用。
- 同日将已分别验证的 Android 窗口类型及 color-buffer GLES=0 两项研究补丁应用于全新固定 gfxstream 源码副本，Android ARM64 Vulkan-only 离线编译中这两个对象成功；标准 GLES3 头解决实验脚手架的格式枚举缺失后，`frame_buffer.cpp` 仍有 20 个涉及未受保护 GLES 类型、读回与条件编译声明的错误，未进入最终链接。未屏蔽调用或关闭窗口检查；配置、完整错误及脚手架差异保存在 `/var/tmp/rme-android-vk-offline.nVNDSuyG`，该研究后端尚未接入 EMU。
- `tools/diagnostics/gfxstream-framebuffer-vkdecl/` 保存进一步的单项研究补丁：将 Vulkan 状态 getter 原样移出 GLES 条件块。独立红绿复验显示 Android GLES=0 的该条成员缺失错误消失，Linux GLES=1 的原版与候选 getter 机器码及重定位一致；Android 对象仍因其他问题编译失败，整个后端未链接，补丁未接入 EMU。
- `tools/diagnostics/gfxstream-angle-bgr10-format/` 保存独立格式声明研究补丁：将固定源码已有的 `GL_BGR10_A2_ANGLEX` 定义原样放入两种配置可见的共享头。独立复验显示 Android GLES=0 对该常量的编译错误消失；原源码格式转换函数切片的 GLES=1 原版和 GLES=0/1 候选对数值、映射及无效格式对照一致。实际 llvmpipe 不支持该格式的 Vulkan 图像创建，Android 对象仍有其他编译错误，不能将上述结果解释为图像读写或完整后端通过；补丁未接入产品。
- 固定候选后端的 `ReadbackWorker` 目前只有 GLES 实现；Vulkan 的 ColorBuffer 直接读回不能直接替代其异步显示读回、回调、显示注册与线程退出流程，且现有部分 `void` 包装丢失底层 Vulkan 读回错误。已有 llvmpipe RGBA 直接读回仅证明特定单次路径。此项尚无经过完整行为验证的修复，未添加空 worker 或虚报读回成功。
- 对照固定 gfxstream 源码与手机实际 Vulkan 查询：Android 平台默认外部内存模式为 AndroidAHB，要求的 AHB、dedicated allocation、memory requirements 2 和 external memory 扩展均在本次手机驱动声明中；此前 llvmpipe 实验使用的 HostAllocation/SystemBlob 路径需要手机未通告的 `VK_EXT_external_memory_host`，不能直接沿用。源码中 ColorBuffer 使用 AHB 分配后导入，而普通 VkBuffer 路径涉及 Vulkan 分配后导出 AHB；手机已测的 BLOB 导入只能证明其特定单进程方向，不能替代对这两条实际路径的验证。固定 Kumquat 对 AHB 的相关描述和句柄传递存在未完成的适配；不修改安全检查或设备 Guest 的前提下，Android 端到端仍未接通。
- 新增仅 Debug 可手动触发的宿主 OpenCL 探针：在 EMU `:vm` 进程通过公开库名加载 OpenCL，选择报告可用且带在线编译器的 GPU，编译内核并检查 64 项结果；入口在宿主日志面板，普通启动不执行，不新增对外入口。独立 WSL 模拟驱动 9 项测试及 Debug/Release 编译检查通过，包括多平台选择和失败对照；厂商驱动调用可能阻塞或崩溃，失败不会伪报通过。诊断 APK `E:/RedMagicEMU/local-data/rme-root-archive-20261117/rme-opencl-host-7uaimsiz/app/build/outputs/apk/debug/app-debug.apk`，SHA-256 `ca5bbe722d46eadc42851beb8ef79575b42ae7be676aec59a763f577f8d992d4`，保留通过安兔兔复测的 GLES 资产及现有安全配置，已覆盖安装。
- 实机运行时，EMU `:vm` 宿主日志面板显示 `OpenCL reported GPU: 64/64 values verified`；结果由电脑 OCR 识别，测试没有提供 GPU 型号。此前手机 shell 探针报告 Adreno 750，但不能以此替代本次进程内型号查询。该结果证明指定宿主应用进程可执行这一简单 OpenCL 内核，不证明 Guest OpenCL 桥、Geekbench GPU、跨进程内存或 Vulkan 已接通。
- 继续建立仅 Debug 手动触发的本地 `socketpair` OpenCL 平台/设备查询往返，使用会话内虚拟 ID，不传宿主裸指针；这一阶段尚未接 Guest，也不执行跨进程计算。独立复验 24 项协议/错误测试、9 项原计算探针测试通过；WSL 真实 clvk/llvmpipe 直查与本地往返查询一致（软件 CPU）。手机覆盖安装 `E:/RedMagicEMU/local-data/rme-root-archive-20261117/rme-opencl-discovery-4km1ypqw/app/build/outputs/apk/debug/app-debug.apk`，SHA-256 `0de747d1fd8b51831dffae12a29070ac3f4f7880b31c105cbbd150477a660c1b`；包内保留此前实机 GLES 资产、radio LF 和现有网络闸门。
- 手机 EMU `:vm` 进程手动运行该探针，宿主日志 OCR 显示本地往返枚举到 `QUALCOMM Adreno 750`、`OpenCL 3.0`、设备类型 `0x4`（GPU），并显示同次独立 GPU 计算 `64/64 values verified`。这证明该手机和应用进程下的本地往返枚举以及独立简单内核运算；仍未证明 Guest OpenCL、Geekbench 或 Vulkan 可用。
- 新增仅 Debug 手动执行的宿主 Vulkan 计算探针，复用现有 64 项计算验证；WSL llvmpipe 软件计算、错误预期、无 ICD、注入 fence 失败及不确定完成状态下禁止同进程重试对照通过。Debug/Release 离线构建成功，Release 不包含探针原生库；Windows 722 项单元测试仍为已记录的 1 项文件锁失败、89 项跳过。合并 Vulkan 和 OpenCL 宿主探针的调试包 `E:/RedMagicEMU/local-data/rme-root-archive-20261117/rme-vkcl-host-cs_v8npl/app/build/outputs/apk/debug/app-debug.apk` SHA-256 `fc145f8169df612884ce5827094b9ab401fed172f957ab2dfeb001620e862420`，保留 GLES 和 radio 正确资产，已覆盖安装。
- 手机 EMU `:vm` 进程手动运行该 Vulkan 探针，宿主日志经 OCR 显示 Adreno 750、集成 GPU、Vulkan 1.3.128，`VULKAN_COMPUTE_OK words=64`、`PROBE_STATUS=OK`。这是宿主应用进程的简单 Vulkan 计算结果，不证明 Guest Vulkan HAL、跨进程资源或 Geekbench 已接通。
- Guest 的 Geekbench 6 Vulkan 成绩出现异常，随后只读核对同次运行日志。`AdrenoVK-0` 明确报告 Geekbench 进程加载 `/vendor/lib64/hw/vulkan.adreno.so`，驱动版本 `0842.23`。Geekbench 同次日志显示 workload `303`、`401`、`406`、`407`、`503`、`504`、`601` 未通过结果校验，因此这些测试结果不能作为有效 GPU 性能成绩。此次确认 Guest 能加载原厂用户态驱动并进入测试流程。Guest 计算正确性和实际 GPU 执行路径当时仍待验证。
- 继续只读检查该实例的运行树：`dev/kgsl-3d0`、`dev/ion` 不存在，`dev/dri` 目录为空；API 36 runner 对 Guest `/dev/ion` 特判为 memfd，Guest 设备路径一般映射到运行树，现有源码未发现 Guest Vulkan 到宿主 Vulkan 的计算桥。以上不能排除其他间接路径，也不能单凭文件状态确定本次校验失败的根因。计时源码检查未发现对 `clock_gettime` 的专用缩放；现有 `GuestRunner` 会将普通诊断程序路径解析到 Guest 运行树，还会尝试写入进程账本，故未向 Guest 放入或运行诊断程序。本轮没有代码修复或新 APK。
- `tools/diagnostics/vulkan-hal-abi/` 离线检查固定 gfxstream Android HAL 入口：在 Linux 将上游原文件按 C 编译并使用明确返回失败的三个测试替身时，模块 `vk0` 打开和三个函数指针检查通过；无效名称触发断言、关闭返回 -1，整体检查如实返回失败。这两项是探索性行为检查。原文件使用 NDK 编译为 ARM64 对象并能看到 `HMI` 符号；没有链接真实后端、运行 Android loader、接入 EMU 或修改设备。此检查不证明 Vulkan 计算可用。
- 本阶段验证 Vulkan 客户端与宿主构建。采用 gfxstream `d7a33801fb557cebd4f8564a8af431669b3f471d` 构建 Android ARM64 客户端和宿主。同源原始 HAL、HostConnection、QEMU pipe 源文件编译为对象，但尚未完成 Vulkan 编码器或 HAL 的完整链接；不同修订的客户端和后端不能假设互通。宿主 GLES+Vulkan 构建复现 Android 误包含桌面 `GL/glx.h`；`tools/diagnostics/gfxstream-android-gldispatch/` 保存单项条件修正及电脑对照，原版 Android 对象失败、修正后编译通过，Linux 对象对照一致。完整后端仍有窗口类型编译错误，Vulkan-only 构建还有平台 GPU 信息符号未解决；没有运行 GLES、修改 Guest、安装设备或完成 Vulkan 桥。
- `tools/diagnostics/gfxstream-android-platform-define/` 保存该固定版本 Android 宿主交叉构建的配置修正：NDK 预定义 `__ANDROID__`，上游原生窗口分支却检查 `ANDROID`；只在 Android C/C++ 构建中加 `-DANDROID`，使用原有 Android 分支。电脑红绿复验中三个对象的原始窗口类型错误均复现、添加宏后编译通过，Linux 类型断言保持不变；全新 Meson 构建同时应用上一条 GLX 条件修正，四个相关对象编译通过。`FrameBuffer.cpp` 越过类型错误后仍缺 `cutils/native_handle.h`，完整后端链接、设备运行和 Guest Vulkan 均未验证；未运行 GLES、修改安全检查或设备。
- `tools/diagnostics/gfxstream-d7a-android-backend/` 记录继续构建：选用同源 Android 原生窗口实现、真实 AOSP libcutils 头、NDK `-landroid` 和真实 composer 解码器后，176 步构建进入严格最终链接，但仍因缺少 `getGpuInfoListNative` 和 `isVulkanSafeToUseNative` 两个 GPU 信息与可用性检查符号失败；未加入无条件放行实现。复验脚本核对窗口源选择、重新编译 FrameBuffer 对象，并核对最终日志恰有这两个未定义符号。同源 Guest 客户端虽然能编译若干 ARM64 对象，但 Vulkan 初始化和宿主可见内存仍依赖当前 EMU 未提供的 VirtGpu/Goldfish 接口，QEMU pipe 本身不能解决；后端与 Guest Vulkan 尚不可用，没有安装设备或修改 Guest。
- 只读核对上述 GPU 检查：系统 Vulkan 枚举可以查询部分设备属性，但无法无损提供现有 `GpuInfo` 的所有字段；空设备列表会使现有黑名单查询无匹配，不能把它当成失败即拒绝。Windows 原实现还检查已知不良驱动或 loader 版本，单凭枚举成功不能等价替代。现有固定版本的检查结果是否被完整后端启动链消费也未证实；此外能力检查应覆盖后端实际选中的设备，不能只检查另一块 GPU。因此本轮未为两个缺失符号添加空实现或无条件放行，也未将 Vulkan 枚举成功等同于安全判定。
- 只读检查 d7a 的非直映射复制路径：Guest 默认始终使用直映射，未建立 EMU 内存后端；备用 `flush`/`invalidate` 对部分映射的 `VK_WHOLE_SIZE` 长度处理需要实际映射区间，但现有客户端没有保存该区间，host 也收不到该元数据。修正需覆盖 map/unmap 生命周期、变换和两端编码，并同步修改生成器；不能只用 allocation 大小代替映射末尾。该备用路径未启用，未证明其导致本次 Geekbench 错误，本轮没有为其添加未经双端验证的补丁。
- 宿主 Vulkan 计算通路要求使用真实 GPU。`native/vulkanbridge/gpu_select.c` 按驱动报告选择支持 Vulkan 1.2 及以上、具有非空计算队列的集成或独立 GPU，并拒绝 CPU、虚拟和未知设备。调用方继续执行原有检查并管理实例生命周期。替身 loader、llvmpipe CPU 拒绝和 ARM64 构建均通过。队列族数量变化的回归在增加复核后通过。手机宿主探针报告 Adreno 750、Vulkan 1.3.128、计算队列族 0，退出码 0。该阶段验证了宿主设备选择，Guest Vulkan 计算尚待接入。
- 在 `native/vulkanbridge/gpu_device.c` 增加使用上述选择器的宿主 Vulkan 设备生命周期组件：调用真实 `vkCreateDevice` 并核对返回值，取得计算队列后向调用方交付；失败时不交付句柄，空队列会销毁已创建设备。伪 loader 覆盖拒绝 CPU、入口缺失、创建设备失败或返回空句柄、空队列清理及重复关闭/重新打开；电脑回归、ASan/UBSan 和 Android ARM64 构建通过。手机宿主使用同源码 CLI 成功创建和销毁驱动报告的 Adreno 750 逻辑设备及计算队列，退出 0，临时文件已删除。调用者须维护 instance 和设备独占生命周期并在销毁前完成工作；此组件尚未接入产品或 Guest，该手机运行未提交计算任务。
- `native/vulkanbridge/gpu_buffer.c` 增加宿主单进程 storage/transfer buffer 的创建、实际内存需求查询、分配、绑定与销毁；包括必需的专用分配、跳过未启用特性的特殊内存类型、错误清理、本组件活跃分配数及活跃 buffer 阻止关闭 device。调用方仍须对设备上所有外部分配统一计数、保证同步与资源生命周期；该组件未实现映射、数据传输或计算。电脑 fake dispatch 负向测试、ASan/UBSan、GPU-only CPU 拒绝对照及 Android ARM64 构建通过；手机宿主在驱动报告的 Adreno 750 上成功创建、绑定并释放 65536 字节 storage buffer，测试程序退出 0，临时文件已删除。此测试未提交 GPU 计算，组件未接产品或 Guest。
- `native/vulkanbridge/gpu_submission.c` 增加宿主单进程队列提交及 fence 生命周期：超时保持 pending 并允许再次等待；等待内存不足保留 pending；等待返回设备丢失时报告失败并只允许按该等待覆盖范围清理；提交结果未知时保留资源且阻止后续任务。活跃提交会阻止本组件 buffer 和 device 销毁，调用者仍须对其他 Vulkan 对象、命令缓冲及队列进行同步和生命周期管理。电脑 fake dispatch、ASan/UBSan、CPU 拒绝及 ARM64 构建通过；最终构建手机宿主在驱动报告的 Adreno 750 上成功执行空提交、等待 fence 并清理，退出 0、临时文件已删除。空提交没有着色器或结果校验，组件未接入产品或 Guest。
- `tools/diagnostics/vulkanbridge-compute/` 将上述选择、设备、buffer 和提交组件连接到单进程宿主 Vulkan 计算测试：手机宿主报告 Adreno 750、Vulkan 1.3.128，真实 shader 运算 64 项逐项校验通过且退出 0；同一程序故意错误预期时报告 1 项不符并退出 1。两次运行使用宿主临时文件，结束后已删除。该测试使用的是手机 shell 进程而非 EMU `:vm` 或 Guest，不证明新组件已接入产品、Guest Vulkan、OpenCL 或 Geekbench。
- 新增 `native/vulkanbridge/gpu_compute.c` 宿主单进程计算组件：调用方提供事先验证的 SPIR-V、入口、一个 storage buffer 和 dispatch 组数；组件创建描述符、管线和命令、提交并等待，保护在用 buffer，处理失败、超时及设备丢失。当前屏障只覆盖宿主写入→计算→宿主读取，其他 GPU 任务间依赖、shader 合法性/feature 与访问边界由调用方负责，不能作为直接暴露给 Guest 的完整 Vulkan 接口。fake 生命周期、错误、ASan/UBSan、CPU 设备拒绝及 ARM64 离线构建通过；同摘要 `926957f5c3bb3b2be5b4b935303062f8c068f3b1353a1c39cb6a240057d8d4ef` 的测试程序在手机宿主 Adreno 750 执行 64 项结果校验通过，错误预期对照按预期失败，临时文件已删除。本项尚未接入 EMU 或 Guest。
- 固定 d7a 候选后端中发现 `createGlobalVkEmulation` 丢弃 `vkCreateDevice` 返回值并检查旧 `res`，可能在创建设备失败后继续初始化。`tools/diagnostics/gfxstream-createdevice-result/` 保存单行修正和对真实初始化函数的电脑隔离回归：注入创建失败时原版错误地继续到设备 dispatch 边界，修正版提前返回；成功替身两版均抵达边界。测试使用 llvmpipe 查询和设备创建替身，不创建真实设备；补丁尚未接入产品，失败后的全局状态清理与重试未修复或验证。
- `tools/diagnostics/gfxstream-physical-device-selection/` 保存针对固定 d7a 候选后端的设备选择研究补丁：只选择驱动报告的集成或独立 GPU、Vulkan 1.2 以上且同一非空队列族同时支持 graphics 和 compute，并使用该队列族创建设备；仅有分离队列族的设备目前明确拒绝。隔离测试调用真实初始化选择逻辑，以 llvmpipe 句柄和模拟属性执行 11 个红绿场景，覆盖 CPU/virtual 拒绝、非零 API variant、多设备及队列族选择；模拟属性不是实机 GPU。补丁尚未接入产品，与此前设备创建返回值补丁独立；未验证 Android 后端、Guest 或 Geekbench。
- 在全新 `/var/tmp/rme-d7a-vk-merged-android.IFuEeE2h/` 隔离副本中，设备创建返回值及物理设备选择两份研究补丁按顺序以零 fuzz 应用，组合后的 `VkCommonOperations.cpp.o` 成功交叉编译为 Android ARM64；完整后端仍在严格链接时因 `getGpuInfoListNative`、`isVulkanSafeToUseNative` 两项缺失失败。未添加空实现或关闭检查；无可用 Android 后端、没有接入 Guest、没有发布或安装 APK。
- 修正 EMU GLES 编码器的一项独立缺陷：`glBufferData` 的 usage 不属于宿主 GLES3 的九种有效值时，原实现仍覆盖本地索引影子；现保留旧影子并继续原样发送调用，让宿主报告错误。完整生产编码器的本机测试在修复前对五个无效值失败，修复后九个有效值、五个无效值及命令字节检查通过；像素存储、缓冲读取和 YV12 既有电脑测试通过。使用既有相机实验构建脚本及 NDK 28.2 重建，宿主实验库摘要仍为 `990c361f73fe9760a2644d7a139a3e4fe2377f22a054298f87883656b3332533`，API 36 GLES 资产更新为 `d8baf388f89e806482976bec06c0085364743036ea36f7231419737c9c0793c0`；替换前资产 `dbc3fcf0d9274bd89199152cb779d4244df1278b8feecbce5695d198ebb18909` 保存在 `%TEMP%/rme-bufferdata-before-884pkl22/libGLES_aemu.so`，核对后可恢复。此项不覆盖 ES2 特定 usage 差异或其他宿主拒绝原因，也不修复 Vulkan；未修改 Guest 或进行设备验证。
- 上述资产使用独立新快照离线构建 Debug APK 成功：`E:/RedMagicEMU/local-data/rme-root-archive-20261117/rme-gles-bufferdata-apk-4hf0wpjt/app/build/outputs/apk/debug/app-debug.apk`，SHA-256 `a303836eba569ca1e36bf63662b8b16719cbbc551192145cc7718d3b02e3bc58`。包内 205 项引擎资产与工作区逐字一致，radio 清单仍为 LF 且 SHA-256 `3140b6d05c2e61decb8a5e9ba27afc98b2e1d03b94e9177360f5dfc25c8bb4d7`，签名与已安装的上一实验包一致；未安装或运行该 APK。

#### 0.6.1 宿主 Vulkan 服务基础（2026-11-17）

- 新增仅供 EMU 自用的宿主服务进程：创建 Vulkan 1.2 实例并选择驱动报告的真实 GPU、创建逻辑设备和计算队列；使用父子进程管道报告就绪及关闭，不提供网络监听、Guest 协议或着色器入口。EMU 仅对启用 GPU 的 NX809J/API 36/ARM64 N64 实例在后台启动该进程，并随实例停止回收；启动失败不会被报告为成功，也不会阻止 Guest 启动。该阶段完成宿主服务就绪检查。
- 服务检查调用方传入的父 PID 并使用 Linux 父进程退出信号；启动超时、强制终止和未确认退出会被记录，未确认清理会保留实例以便重试。WSL 使用模拟 Vulkan 的 6 项进程测试、4 项父进程死亡测试及真实 llvmpipe CPU 拒绝对照通过；这不能替代 Android 应用进程或驱动卡死场景的实机验证。独立输出目录的 23 项 JVM 定向测试通过，覆盖服务、实例协调和原有 GuestVm 初始化；完整 Windows 测试中 `InstanceRuntimeLeaseTest.anotherProcessBlocksAndDeathReleasesLock` 在子进程退出后重新取锁失败，原因未确定，未宣称全套通过。
- Windows NDK 28.2 通过 `native/vulkanbridge/build_host_service.ps1 -Ndk <NDK目录>` 可重建 ARM64/API 29、16 KiB 对齐的服务，脚本默认输出到新建的临时目录且拒绝覆盖指定的现有文件；打包文件 `app/src/main/jniLibs/arm64-v8a/libaemuvkhost.so` 的 SHA-256 为 `f0bea08341871fd4bdf817e29fa9eef1978274e00d304eeee43abce97ded80ff`。需要回滚时，仅撤销 `GuestVm.kt` 中本项宿主服务接线，移除本项新增的宿主管理器、协调器及该打包二进制并重新构建；保留已有 Vulkan 研究组件及无关改动。
- 独立 Debug APK 在 `%TEMP%/rme-vk-service-final-aaf5180aa97940e486b0da066871d4b0/app-build/outputs/apk/debug/app-debug.apk`，SHA-256 `67d6f039ab7abb5bb23872c386e816656aa1d337508ae452cd144f4175ebacb6`；包内服务与上述二进制一致，205 项引擎资产与当前工作区逐字一致。此版本没有安装或在手机上运行，没有修改用户导入的固件包、Guest 文件或宿主系统；APK 提取后能否在目标手机应用环境执行尚未验证。现有 Guest 应用的 Vulkan 客户端和命令、资源与同步通路仍缺失，不能用于宣称 Geekbench GPU 可用。
- 宿主 GPU 队列选择现在在首个合格物理设备内优先选非空的 graphics+compute 共用队列；没有共用队列时仍选择原来的首个非空 compute 队列，不改变物理设备排序，也不保证回退队列支持图形。新增模拟设备测试在旧逻辑选中队列 0 而预期为 1 时失败，修复后通过；其余设备、buffer、提交、计算模拟测试、llvmpipe CPU 拒绝、10 项宿主服务进程测试和 Android ARM64 构建通过。旧逻辑的源码按本轮反向差异重建于 `%TEMP%/rme-vk-queue-before-652037aed05740799f2e4209f88b7c9f/gpu_select.c`；该目录还保存了改动前服务二进制，恢复前须核对摘要。重建后的服务 SHA-256 为 `1b8f6c40726644244c338c7b2f3b2f442aa78e08c7945cb18fa19f2b784e171d`。
- 新的独立 Debug APK 为 `%TEMP%/rme-vk-queue-apk-bd67d697c72c4692a5177c56c3691088/app-build/outputs/apk/debug/app-debug.apk`，SHA-256 `53f8e3c61c6046afce975e9d6c1e57ac4307eb949565acd17c8280eede5b1bce`；包内服务与重建文件一致，205 项引擎资产与当前工作区逐字一致。未访问手机或安装 APK；此项只修正宿主队列选择，不接通 Guest Vulkan。
- 在 `native/vulkanbridge/gpu_device.c` 增加可选的 Vulkan 1.0 核心设备特性请求：仅查询当前已选物理 GPU，逐项拒绝不支持的特性，设备及队列均创建成功后才报告实际启用的特性；非零请求规范化为 `VK_TRUE`，原接口仍以 NULL 特性创建，不查询或启用任何可选特性。调用方仍须保证所请求特性组合满足 Vulkan 依赖；此接口不处理 1.1/1.2 特性链、扩展、其他 GPU 回退或 Guest 着色器。当前宿主服务仍使用原接口，新接口没有 Guest 调用方。
- 电脑端模拟 Vulkan 测试覆盖 55 个核心特性逐项拒绝、请求规范化、查询缺失、创建失败、空队列及旧接口兼容；相关设备、buffer、提交和计算测试、真实 llvmpipe CPU 拒绝、10 项宿主服务进程测试、ASan/UBSan 和 Android ARM64 交叉编译均通过。Windows NDK 重建服务二进制 SHA-256 `b4d10f7a269ff9361f7ac23d29a6999b11229a232d377e8030bc79c96ee63259`；改动前的 `gpu_device.c`、`gpu_device.h` 及服务二进制备份在 `%TEMP%/rme-vk-features-before-87956ee418564602aa5b8f1dd26e36b0/`，回滚前核对摘要并同步撤销本项测试及说明。没有重建或安装 APK，也没有连接手机或修改导入包、Guest 文件；这些结果不证明 Guest Vulkan 可用。
- 固定 gfxstream `d7a33801fb557cebd4f8564a8af431669b3f471d` 客户端的 `VK_HOST_CONNECTION` 在连接为空时先解引用再检查编码器。`tools/diagnostics/gfxstream-null-host-connection/` 保存最小研究补丁和真实源码宏的独立回归：原版三种返回类型均发生 SIGSEGV，修正版对连接为空、编码器为空和正常连接共 9 种情况通过。修正版诊断源码使用 NDK/AOSP 头编译为 Android ARM64 对象，消除临时路径后独立重建 SHA-256 均为 `158aef0824733dcbc3919b59db47acc971accd257a079b68ebb1cea0dccc4efd`。该补丁通过宏级测试，仍为独立研究补丁。
- 对同一固定版本的 18 个 Vulkan 编码器源文件完成 ARM64/API 35 对象级交叉编译；其中 16 个使用同源 Mesa 诊断头，另外两个在其前使用 NDK 和 AOSP Android 16 硬件缓冲头，混用头文件的 ABI 尚未验证，不可用于运行或发行。清单、源码/对象摘要和逐项重建命令记录于 `/var/tmp/rme-encoder-aosp-headers-1_c96kym/OBJECTS-MANIFEST.txt`；三个代表对象在独立目录重建后逐字一致。可重定位合并后仍有 122 个未定义符号，包括标准运行时及 VirtGpu、GoldfishAddressSpace、Stream 等依赖，这一步仅生成合并对象。未操作手机、修改 Guest 或导入包，也未执行此前被拒绝的双端映射回归。
- 进一步使用 `tools/diagnostics/gfxstream-d7a-vulkan-encoder/` 对这 18 个编码器源统一编译参数和头文件顺序，避免上一次混用头文件：NDK 硬件缓冲、data_space、rect 和已有 AOSP Android 16 vndk/graphics 头均以实际编译依赖核对。旧 Mesa 硬件缓冲头导致两个对象缺少格式常量的失败已独立复现；统一头顺序后，三次独立构建的 18 个 AArch64 REL 对象及输入摘要一致，合并对象 SHA-256 均为 `65650c14dce86ec3740c857b933102504cceab44660c06f4a59324da08877c2f`。仍实际引用两个同源 Mesa 硬件兼容头 `hardware/hardware.h`、`hardware/hwvulkan.h`，其 Android 16 ABI 未验证；合并对象仍有 122 个不同未定义符号，按名称粗分项目/Mesa 36、C/C++ 及编译运行时 84、Android 平台 2，不能当作共享库已链接。脚本依赖本机固定版本源码、既有生成头及工具链，非独立发行构建；未操作手机、修改 Guest 或导入包，也未运行客户端。
- 使用 `tools/diagnostics/gfxstream-d7a-base-link/` 对固定版本 `Android.bp` 中的真实 `Stream.cpp`、`StreamSerializing.cpp` 和 `AndroidSubAllocator.cpp` 交叉编译，并仅以 `ld.lld -r` 与上述编码器对象合并核对符号。原 122 个未定义符号有 14 个由这三个对象提供，但它们引入 25 个新未定义符号，合并后共 133 个，包括 MemStream 和 C++ 运行时；三次独立构建得到相同合并对象 SHA-256 `0bbf21e731f1c9a2bfdaf7c2690e68dd6ac0d453b354ac37c2918225a010cf4c`；脚本记录输入、命令和符号差集。
- 静态审查发现上述上游 Stream 在短读、无容量参数的 buffer 读取及无上限的数组数量方面存在边界限制，SubAllocator 的越界检查只记录信息；本轮没有运行这些代码，不能把它们直接暴露给不可信 Guest。现有两个 Mesa 硬件兼容头的 Android 16 ABI、客户端加载、传输和剩余依赖仍未解决。未操作手机、修改 Guest 或导入包，也未执行此前被拒绝的双端映射回归。
- 使用 `tools/diagnostics/gfxstream-d7a-memstream/` 编译固定版本真实 `MemStream.cpp` 并与上一编码器加基础对象合并；`llvm-nm` 未定义符号从 133 个降为 131 个，恰好解决 `MemStream::load/save` 两项，没有引入新的未定义符号。进一步使用 NDK ARM64/API 35 的 `libc++_shared`、`liblog`、`libandroid` 链接接口和 `--no-undefined` 做真实共享链接对照：原对象报告 25 个不同的未定义符号，加入该对象后降为 23 个，链接仍失败且没有生成共享库。三次独立构建的合并 REL 摘要均为 `e4477501de21dbef459498600796f90c5d953ed32dcd95f22c845d8966beb9e5`；此结果不包括完整 Vulkan 客户端、应用入口或宿主解码器。
- 对上游 MemStream 的静态审查发现 `load` 忽略缓冲读取失败，流内长度可驱动无上限分配；本轮只编译并分析符号，没有执行该组件或将它暴露给 Guest。未操作手机、修改 Guest 或导入包，也未重试此前被拒绝的双端映射回归。
- 使用 `tools/diagnostics/gfxstream-d7a-vk-object/` 交叉编译固定版本真实 Mesa `vk_object.c` 及其 sparse_array、hash_table、ralloc、u_printf 和既有生成 enum 源，并与上一编码器对象做严格 Android 链接对照：`vk_object_free/zalloc` 两项缺失得到解决，严格链接错误 23→21，仍无共享库；`llvm-nm` 未定义项因新增库依赖由 131→139。三次独立构建的合并 REL 摘要为 `71c0653cf47a41f71d38b48dfe120b725e3bf73514a55df89b4a964d9cbab663`。该构建未链接上游 WSI dummy 同步对象，因为其真实宿主等待路径尚未验证；固定版本生成头、Android 宏、两个 Mesa 硬件兼容头及实际设备 ABI 均未完整验证，未操作手机、Guest 或导入包。
- 复核发现此前编码器手工配置没有定义 Android.bp 默认开启的 `GFXSTREAM_ENABLE_GUEST_GOLDFISH`：宏 0 且无 Virtio native sync 时部分 fence/QSRI 分支可能未写入 FD 却返回成功；宏 1 则编入依赖 `/dev/goldfish_sync` 的 open/ioctl 路径，且部分调用未检查返回。`tools/diagnostics/gfxstream-d7a-goldfish-config/` 对宏 0、宏 1、宏 1 加 Android.bp 部分编译选项各编译全部 18 个 ARM64 对象；三次独立重建的 54 项对象和输入摘要一致，宏 0 全部复现旧对象，单改宏只改变 ResourceTracker，加入其余选项又改变两个对象。该构建保留手工参数、旧生成头和两个 Mesa 硬件兼容头，非完整 Soong 或目标 ABI 验证；未运行对象、访问设备节点、修改 Guest 或导入包，也未重试此前被拒绝的双端映射回归。
- `tools/diagnostics/gfxstream-d7a-fence-fd/` 为固定 gfxstream `on_vkGetFenceFdKHR` 保存单项研究补丁：宏 0 且无 native sync 时返回 `VK_ERROR_UNKNOWN`，不清除原 fence 的 FD 状态或读取未写入的输出。提取真实完整方法并使用进程内替身的测试在原版对 SUCCESS/NOT_READY 两种 fence 状态复现错误成功及所有权变化，补丁后宏 0/1 和 native 成功/失败等对照通过；补丁后的完整 ResourceTracker 分别以宏 0/1 编译为 ARM64 对象并独立复验摘要一致。该补丁未接入 EMU；QSRI 和宏 1 的 Goldfish queue_work 失败处理仍未修复，未访问真实 fence FD、手机、Guest 或设备节点。
- `tools/diagnostics/gfxstream-d7a-qsri-fd/` 保留未完成的 QSRI 研究测试及候选补丁；替身测试提示无同步后端可能错误报告成功、native 导出失败时可能使用未初始化的局部 FD，但受控替身的数值不能推断实机表现。候选返回 `VK_ERROR_UNKNOWN`，独立源码审查发现该值可能在 Android present 结果聚合中触发取决于构建配置的断言；目标固件的具体调用链和构建配置未确认，因此该候选禁止接入或打包。未运行 Guest、真实 fence FD 或设备节点。
- 通过 EMU 正常入口启动既有实例，并用 Root 输入触发「重试」，Guest 到达锁屏。只读检查发现，在未手动启动 Geekbench 时，该 Guest 的 zygote 已映射其运行树内 `/system/lib64/libvulkan.so`；因此在应用启动后才试图改变首次库加载路径不能直接替换该已有映射。此观察不证明其首次加载时序、所有进程的加载行为或新的客户端可用；没有修改导入包、宿主系统设置或真实硬件节点。

#### Vulkan 客户端连接（2026-11-17）

- 新增 `native/vulkanbridge/client/emu_pipe.c`：使用 Guest 本地 Unix socket 发送上游 `opengles` 或 `GLProcessPipe` 连接前缀及后续字节流。电脑端测试覆盖两种连接、收发、服务缺失和断线，Android ARM64 对象编译成功。测试对端不包含渲染器；客户端尚未完整链接、打包或接入 Guest，宿主解码及同步仍未完成。没有修改原始导入包或手机系统。
- 客户端连接层和本地同源 AndroidPipe 进程服务的集成测试取得真实非零进程 ID，断连后服务端回收会话；测试未初始化渲染器，且同 PID 授权仅适用于测试。`gfxstream_vk_CreateInstance` 的只读输入改写及若干失败路径资源清理问题已在 `tools/diagnostics/gfxstream-instance-create/` 形成候选补丁，通过提取上游函数的回归测试并编译 Android ARM64 对象，尚未实机验证。
- 候选客户端重新使用一致的 Android 平台宏构建同源 Mesa runtime、通用 WSI 和部分编码组件；目前严格链接仍有未解析依赖。通用 WSI 不包含 Android 窗口交换链；Guest 图形缓冲与宿主 ColorBuffer、同步栅栏，以及 VM 的真实渲染器初始化和实例授权仍未接通。尚不能输出 Guest Vulkan 画面。
- 同源 AndroidPipe 通道新增唤醒、Unix socket 会话和主屏参数接线；真实进程管道测试、连接限制测试及缺失回调时拒绝启动的测试通过，相关对象交叉编译为 ARM64。主屏参数目前只接收调用方提供的数据，尚未接产品 VM；没有启用渲染器或替换现有 GLES。
- `tools/diagnostics/gfxstream-device-uuid/` 验证渲染器的实际物理设备选择可与外部提供的设备及驱动 UUID 绑定；合成设备数据测试和 ARM64 编译通过。真实 provider 及 Android GPU 信息判断仍未实现，该补丁未接入产品。
- 修正候选客户端 QEMU 进程管道：初始化失败时保持可重试并向 HostConnection 返回失败，正确处理有效 fd 0。基于实际源码提取的 9 个测试场景通过，修正已接入电脑端候选构建并通过 ARM64 严格链接；尚未安装或验证 Guest Vulkan。
- 修正候选客户端 `vkAcquireImageANDROID` 的 fence 等待失败处理：失败时关闭传入的 fence、释放已持有的锁并返回错误，不再继续提交；电脑端提取实际源码的替身测试通过，18 个编码器编译及诊断链接通过。真实 fence、Guest 窗口和 GPU 同步尚未验证。
- 候选客户端新增 Android 原生窗口的创建、销毁与实例分配器分发，并根据目标 Android 接口连接窗口；CPU 分发测试及 ARM64 离线链接通过。swapchain、图像队列和呈现仍缺失，相关扩展暂不公开；未在手机或 Guest 执行。
- 添加候选的 Android 窗口图像队列、slot 及 acquire/present 内部流程；CPU 替身测试验证了 fence 复制、转交与失败清理，Android ARM64 编译和离线链接通过。尚缺有效的 Guest 图像与宿主 ColorBuffer 资源连接，不公开不完整扩展，未运行真实 fence 或 GPU。
- 候选的 swapchain 生命周期已接入直接 Vulkan 分发，但真实共享图像 provider 尚缺，创建时返回错误，相关扩展仍不公开。宿主 gfxstream 与 AndroidPipe、显示及生命周期候选对象在不启用额外 UUID 实验、保留现有设备选择检查并加入 GPU 信息候选时，离线严格链接仍缺原有 `isVulkanSafeToUseNative` 符号；没有生成可运行的宿主后端。
- Android Vulkan GPU 信息候选从 loader 查询物理设备属性，合成设备测试及 ARM64 编译通过；上游入口为 void，真实接入仍须检查独立失败状态并保留驱动判断，当前未接产品或运行真实 GPU。
- 进一步修复候选 Vulkan 客户端的窗口查询分发：EMU 自有 surface 不再被 Mesa 当作其他布局解读；没有真实可呈现 provider 时返回错误。原生 buffer 图像创建也会拒绝无连接或无效宿主句柄。相关 CPU 回归、ARM64 编译和离线链接通过；真实共享图像与 Guest 渲染仍未实现。
- Android native-buffer 版本枚举已修正为当前编码器实际提供的 spec 7；修正后离线测试和 Android ARM64 链接通过。宿主单屏 pose 及 GPU 信息查询失败传播已补充候选接线；宿主严格链接仍因缺少真实 `isVulkanSafeToUseNative` 实现而失败。以上未在 Guest 或真实 GPU 运行。
- 修正候选客户端 fence 导出对 Goldfish 队列失败的处理：返回错误并保留调用方输出和已记录状态。实际函数提取的替身测试先失败后通过，18 个 encoder 均重新编译为 ARM64 并完成离线严格链接；未运行真实 fence、Guest 或 GPU。
- 新增隔离的 Android Vulkan surface 候选，按实例获取真实 `vkCreateAndroidSurfaceKHR` 并仅在扩展可用时启用；合成 dispatch 测试与 ARM64 编译通过。宿主渲染器、Surface 生命周期、Guest swapchain 图像身份与 present 协议仍未完整接通；未安装到手机、未修改宿主系统或 Guest 包。
- 新增 EMU 应用内独立 Vulkan 窗口的 JNI 引用管理候选；替身测试与 ARM64 编译通过。该接口尚未接 UI 或打包，Surface 销毁前同步停止 swapchain 的握手和真实 Guest Vulkan 实例绑定仍未实现，不能显示画面。
- Vulkan 客户端候选结合真实上游属性实现、编码组件和只读提取的目标固件系统库后，通过 `--no-undefined` ARM64 离线链接。此前一次链接使用了 Android 配置不完整的 HostConnection，后来重编并补齐真实图形辅助组件和系统窗口库后重新通过。产物依赖 NDK 与系统两套 C++ 运行库以及多个平台私有库，Android linker namespace、运行时 ABI、加载入口、同步和窗口链路均未验证；没有打包或安装。

- 电脑端新增 EMU 私有 `:vulkan` Surface Binder 服务，使用 Android `Surface` 传递窗口，服务未导出且目前始终返回未就绪；AIDL、Kotlin 编译和窗口引用释放替身测试通过。服务尚未绑定界面、JNI 或渲染器，也没有完成窗口销毁同步及进程退出检测；本轮未安装或操作手机，Guest Vulkan 仍不能显示画面。
- 隔离的纯 Vulkan 后端构建修正了 Android ARM64 原生窗口类型，类型测试先失败后通过，全部对象已编译到严格链接阶段。加入 GPU 信息候选后仍缺实际的驱动判断实现；该构建使用窗口 stub，尚不能呈现画面，也没有安装或运行在手机上。
- 隔离的 gfxstream 句柄候选修复了反向索引删除及不同对象类型共用同一原始值时互相覆盖的问题；真实实体容器的电脑端测试和 Android ARM64 编译通过。同类型跨设备碰撞、交换链图像登记及运行时隔离尚未解决，该候选没有接入产品。
- 候选客户端在窗口队列提交失败后将交换链标记为失效，后续 acquire/present 返回过期错误，销毁时仍归还已取得的缓冲；失败测试先复现重复提交，再通过完整 CPU 窗口流程回归及 ARM64 编译。真实窗口、fence 和 GPU 尚未验证。
- 修正候选 Surface 接口的 C/C++ 链接声明：跨语言链接先失败后通过，Android ARM64 同库严格链接及引用管理测试通过；尚未接入实际解码器。另有图像视图设备匹配隔离候选通过替身回归与 ARM64 编译，交换链图像登记和呈现仍未完成。本轮没有操作手机或修改导入包。
- 隔离的交换链元数据候选在实际 decoder 成功创建后保存设备与图像基础参数、复制队列族数组，并在销毁或设备清理时删除；原版失败、候选通过 CPU 替身回归，两个完整 decoder 文件通过 Android ARM64 编译。扩展链、图像登记、快照和显示仍未实现，候选尚未接入产品。全量 `:app:testDebugUnitTest` 执行 745 项，1 项 `InstanceRuntimeLeaseTest.anotherProcessBlocksAndDeathReleasesLock` 在子进程退出后重取锁时报占用，89 项跳过；该项与本轮图形修改无直接关联，未修改。
- 隔离的交换链图像登记候选可在限定条件下保存稳定 box，处理不完整枚举并在销毁时保留驱动拥有的图像；实际方法提取的 CPU 替身测试和 Android ARM64 编译通过。普通图像后续创建可能覆盖同值记录，客户端加载、Surface 协议和 present 尚未接通；候选未部署，不能显示 Guest 画面。
- 交换链登记已迁入隔离的 decoder 生成流程，并与 Surface 查询、销毁和 present 返回值候选联合生成；相关 CPU 替身测试、Guest 7 个和宿主 9 个 ARM64 对象编译通过，宿主对象可重定位合并解析了 Surface 序列化符号。尚无 Surface 创建、协议协商、最终渲染器链接或真实 GPU 运行，不能开放 WSI。纯 Vulkan 启动控制候选通过实际 Guest 编码与进程管道替身测试，但尚未接入产品。

#### E 盘构建快照整理（2026-11-17）

- 将本次工作留在 E 盘根目录的 10 个 `rme-*` 构建快照目录及 3 个对应 tar 原样移入 `E:/RedMagicEMU/local-data/rme-root-archive-20261117/`；原路径和新路径记录在该目录的 `original-paths.csv`。此项仅移动，没有删除或释放空间；如需回滚，可按清单将对应条目移回原路径。项目进度中 9 个受影响的 APK 路径已更新，9 个 APK 的 SHA-256 与原记录一致；其他目录、用户固件和手机未改动。

#### Guest Vulkan 宿主后端严格链接（2026-11-18）

- Android Vulkan-only 候选原先因缺少 `isVulkanSafeToUseNative()` 无法严格链接。现在复用固定的 64 项 Vulkan 计算和读回，后端只选择该次计算对应的设备和驱动 UUID，保留原有黑名单及设备检查。新入口在替身测试覆盖通过、黑名单拒绝、空 UUID、计算错误及未完成提交，另以真实 llvmpipe 验证 CPU 设备被拒绝。
- 固定 gfxstream d7a Android 后端首次用本轮入口完成 `--no-undefined` 严格链接。产物 `build/diagnostics/rme-vulkan-preflight.q5lYSKNw/backend/libgfxstream-wsi-candidate.so` 为 AArch64，LOAD 对齐 16 KiB，依赖 Android Vulkan loader。SHA-256 `053e1f29d58d5bccc56fe7f5288303e6490b3151bbd0f5b441404e7a6d6ec820`。重建和回滚见 [GPU 信息与启动计算](../tools/diagnostics/gfxstream-android-gpuinfo/README.md)。
- WSL 在现有独立 Kumquat 路径复验了 Vulkan 64 项计算、OpenCL 64 项计算和工作组规约，均通过。宿主实际为 llvmpipe 软件 Vulkan，此结果不属于上述 ARM64 后端。现有另一套宿主服务的进程回归 14 项中 2 项因缺少预期的设备日志行失败，1 项在 stderr 已满后等待退出超时；本轮未修改该服务。
- ADB 当前没有在线设备。候选后端未接入 APK 或手机运行，仍需接通 EMU 宿主会话、Guest Vulkan 加载与传输，并以 Adreno 上的实际计算结果验收。没有改动固件或运行树。

#### Guest Vulkan 计算通道绑定（2026-11-18）

- 单个已认证进程的真实 `GLProcessPipe` ID 绑定到 Vulkan-only 渲染通道。工作线程取得宿主进程令牌，再用真实 ID 校验 `rcSetPuid`。进程资源存在且令牌有效时才能进入 Vulkan 解码。进程管道断开会停止接收、撤销令牌并关闭图形 socket，在调用方完成渲染线程排空前保留进程管道。运行时仍需 EMU 提供认证及排空函数。
- WSL 实际 EMU Unix socket 客户端和同版本 AndroidPipe 验证了 ID 正配、错配拒绝、撤销后拒绝和排空回调顺序。上游 RC 编码器单独复验通过。这些测试没有启动真实渲染线程或 GPU。配对后的 ARM64 宿主候选库严格链接通过，16 KiB 对齐。产物 `build/diagnostics/rme-vulkan-bound.m05FAXjz/backend/libgfxstream-wsi-candidate.so`，SHA-256 `f66babe6ab03ea8ccae3aa6dc7d181a0263ff9b0ac931b714549b325f7e98f10`。重建及回滚见 [Vulkan-only RenderControl](../tools/diagnostics/gfxstream-vulkanonly-rc/README.md)，使用上一节构建参数可重建上一版。
- 本轮选择先接计算通道，WSI 仍关闭。APK 尚待接入宿主生命周期及 Guest 客户端加载。ADB 无在线设备，Adreno 及 Guest OpenCL 的端到端运行仍待验证。

#### EMU Vulkan 宿主启动测试 APK（2026-11-18）

- 在可选 Debug 构建中接入目标 VM 的 gfxstream 宿主子进程。它使用现有显示参数、GPU 选择和 VM 停止流程，只在 Vulkan 设备有效后向 EMU 报告就绪，并通过 `RedMagicEMU-Vulkan` 记录实际设备名及版本。此启动试验暂未开放 Guest 连接。上游无窗口关闭时访问空 PostWorker 的问题先以提取实际分支的空指针检查复现，修复后分支测试通过。
- APK 位于 `build/diagnostics/rme-vulkan-startup.54yZOWIw/redmagicemu-vulkan-host-startup-debug.apk`，SHA-256 `0c886600879fe5800b4b4892b788bcf39c1cd730bd3e49b6b14f963fbe0d069d`。独立 `:app:assembleDebug` 成功，签名和包名校验通过。7 个 ARM64 宿主文件的 APK 解压摘要与构建清单相符，16 KiB LOAD 对齐；205 个引擎条目与工作区逐字一致。定向测试 22 项通过，全量 746 项中有既有 Windows 问题 `InstanceRuntimeLeaseTest.anotherProcessBlocksAndDeathReleasesLock` 失败，89 项跳过。
- ADB 无在线设备，未安装或验证手机上的真实启动和停止。仍需完成 Guest Vulkan 客户端加载、端到端 GPU 命令与 OpenCL 接入。构建与回滚见 [Android 调试启动](../tools/diagnostics/gfxstream-androidpipe-channel/README.md#android-debug-renderer-startup)。回滚 APK 时省略 Gradle 属性重建，现有固件和运行副本无需还原。

#### EMU Vulkan 宿主启动真机验证（2026-11-18）

- 无线 ADB 连通后核对设备现装 APK 与新包签名相同，备份现装 APK 后执行覆盖安装。设备安装后的摘要与上述启动测试包一致。覆盖安装前的 APK 保存为 `build/diagnostics/rme-vulkan-startup.54yZOWIw/device-before-startup-test.apk`，SHA-256 `fc145f8169df612884ce5827094b9ab401fed172f957ab2dfeb001620e862420`。
- 从 EMU 入口启动既有 GPU 已启用的 NX809J/API 36/ARM64 实例，通过 Root 输入触发「重试」。宿主应用的 `:vm` 进程启动 Vulkan 子进程，日志报告 `Adreno (TM) 750 (Vulkan 1.3.128)`。子进程实际映射了 `libgfxstream_backend.so`、Android `libvulkan.so` 与 `vulkan.adreno.so`。Guest 在同次运行中报告 46 秒完成启动。
- 通过 Guest 的关机请求测试停止。Vulkan 子进程在 `:vm` 父进程仍存活时消失，未见 Vulkan 清理失败，Guest Keeper 服务已停止。测试包保留在手机上，Guest 已关机。该轮验证宿主启动和停止，Guest Vulkan 指令与 OpenCL 仍未接入此包。

#### Guest Vulkan 设备枚举真机验证（2026-11-18）

- 一次性宿主进程以调试应用 UID 启动实际 N64 Guest 探针。探针用已链接的 gfxstream Vulkan 编码器，经 EMU Unix socket、真实进程管道和宿主 decoder 查询物理设备。Guest linker 路径由 N64 已有的进程级 `AEMU64_LIBPATH` 提供，候选库只装载到探针进程。
- 两次实机运行均退出 0。Guest 的实例创建、物理设备枚举及属性查询成功，返回 `Adreno (TM) 750`、vendor `0x5143`、device `0x43051401`、Guest API `1.3.0`；宿主选卡日志报告实际驱动 `1.3.128`。停服对照连接以 `-ENOENT` 失败且进程退出 1。已有 Unix socket/PUID 回归通过，已核对 socket 和手机上的测试暂存文件清理完成。
- 真机产物为 `build/diagnostics/rme-guest-encoder.cEKdO2qU/build/guest/rme-vulkan-device-probe`（SHA-256 `36a675cec12a9596e22269b1baf40180f518b69555ce1e85d45d5fb19e2623e3`）和 `build/diagnostics/rme-guest-vk.lr60duLl/host/jniLibs/arm64-v8a/libgfxstream_backend.so`（SHA-256 `07b42bcf4a79a6230f683d4bc33418761f54b3a520a61f5a765aa71cb85c60a7`）。[重建和运行说明](../native/vulkanbridge/client/EMU_PIPE.md#guest-device-enumeration-probe)。
- 本次沿用已安装的启动测试 APK 和原厂 ROM。普通应用 loader、着色器计算与读回、OpenCL 和呈现仍待验证。

#### Guest Vulkan 计算及查询读回真机验证（2026-11-18）

- 沿用 N64 探针、gfxstream 编码器及独占进程的 Unix socket。在 Guest 中创建 Vulkan 设备和两条计算管线，经 `vkCmdUpdateBuffer` 上传 64 项输入。GPU 上第一条管线计算 `3 * input + 7` 并写入间接派发参数，第二条管线使用该结果派发。Guest 等待 fence 后调用 `vkGetQueryPoolResults`，用两个已知直接派发校准计数，再逐项验算。两份 SPIR-V 均通过 Vulkan 1.1 验证。
- Adreno 750 两次实际运行通过，Guest 两次取得 64/64 正确结果，校准为每工作组 64 次调用。故意改错第一项预期时，Guest 读到 1408、预期 1472，只报 1 项不符且退出 1。既有 Unix socket/PUID 回归通过，手机上的测试暂存及 socket 已清理。APK 与固件未替换。
- 探针 `build/diagnostics/rme-guest-compute-client.D5zyUBBA/build/guest/rme-vulkan-device-probe`，SHA-256 `3e501310255f35562c105e123e630fc9811265decf6ac499fe875b17ab18b7c9`。宿主库 `build/diagnostics/rme-guest-compute-host.O28T4c7z/host/jniLibs/arm64-v8a/libgfxstream_backend.so`，SHA-256 `acb42cd9ccf711cce239617c5a78c703bc604eb41acf5a56529345615465701b`。[重建及正负对照说明](../native/vulkanbridge/client/EMU_PIPE.md#guest-device-enumeration-probe)。
- 此结果覆盖 Guest 提交的真实 Vulkan compute 和基于管线统计查询的读回。通用缓冲区映射、普通 Guest 应用、OpenCL 及呈现仍待接入验证。

#### Guest Vulkan 缓冲区字节读回真机验证（2026-11-18）

- 复用上轮 ARM64 宿主库。Guest 等待 Adreno 750 上的计算 fence 后，用同源 gfxstream 编码向现有复制路径请求一段显式的 768 字节范围，偏移为 0。宿主从已映射的 host-visible Vulkan 内存回传数据，Guest 从 socket 接收并检查 64 个运算结果和同一范围内其余 128 个字。
- 两次实机均返回 `GUEST_VULKAN_BUFFER_READBACK_OK values=64 bytes=768` 并正常退出。错值对照在第 0 项读到 22、预期 23，只报告一项不符，退出 1。原有查询式计算用例重跑 64/64 正确。手机上的测试暂存及 socket 已清理。
- 探针 `build/diagnostics/rme-guest-readback-client.irOgqhwC/build/guest/rme-vulkan-device-probe`，SHA-256 `7bbe3870b72968b5dca30cc48667d5ccd53a115080821bfbb6673767080afd6b`。[使用说明](../native/vulkanbridge/client/EMU_PIPE.md#guest-device-enumeration-probe)。此轮验证了探针的实际缓冲区字节回传。应用侧 `vkMapMemory` 生命周期、写回、普通 Guest 应用及 OpenCL 仍待接入。

#### Guest 公开 Vulkan 接口与共享缓冲区真机验证（2026-11-18）

- Guest 公共 Vulkan 客户端通过 Unix socket 绑定宿主签发的进程 ID，枚举 Adreno 750 和 Vulkan 1.3。旧宿主上同一公共接口探针在实例版本查询失败。新宿主对已认证通道提供标准 Vulkan 调用，跳过该 Guest 没有的 VirtGpu capset。
- 在本机 Adreno 750 上验证 host-visible coherent BLOB 导出。宿主使用 AHardwareBuffer 导出的数据 FD，Guest 使用 Linux DMA-BUF 映射并按 CPU 访问方向执行同步。先验证独立跨进程试验，再接入同源 gfxstream 的分配、映射和回收。公开接口探针使用两个缓冲区，分别核对 GPU 读入的 64 个 Guest 写入值和 GPU 写回的 64 个值，并正常销毁设备。
- 完整公开探针最初缺少同源 command pool 对象，随后发现 Mesa 运行库与客户端的 futex 宏不一致，销毁设备时访问了错误的队列链表偏移。补齐 command pool 对象并统一构建后，探针通过。旧传输可空 layer name 解码修正后，同次实机探针同时枚举 6 个实例扩展并通过缓冲区测试。
- 调试启动使用应用管理器返回的 Geekbench PID，并检查该进程的 Guest 根路径及真实运行器，随后由宿主持 pidfd 和 socket 对端凭据绑定进程。确认安装的 Geekbench 6 已经创建宿主 Vulkan 实例。该次 Android 界面的 API 下拉框仍只列 OpenCL，尚无完整 Geekbench Vulkan 成绩。
- `stage_guest_vulkan_runtime.py` 可重建或回退测试实例的 Vulkan 兼容库。原始导入固件保留，运行树原版 `libvulkan.so` 的 SHA-256 为 `2c6f896fb194b656acf18a5a4405a536b8acb467bbe8213f97acfac60289c296`，已实际恢复并核对过该摘要。当前实验映射只验证了这台设备的 BLOB 布局、缓冲区和单进程计算，同步、图像资源和其他宿主仍待覆盖。
- Geekbench 6.7.1 在计算 API 初始化时也查找五个 Android Vulkan 交换链入口。实例级查找补齐这些真实入口，设备级查找继续核对呈现支持。修正后，GPU API 下拉框重新显示 Vulkan，Geekbench 在宿主 Adreno 750 上创建了计算设备。
- 将原生缓冲区统一转换成 AHardwareBuffer 的试验在绑定设备本地内存时返回 5。加入设备本地缓冲区后，公开 API 探针复现了同一错误。改用 `RME_VULKAN_SHARED_MEMORY=cached` 后，原生 Vulkan 驱动直接决定缓冲区内存要求，Guest 通过 memfd 映射 CPU 缓存页，显式 flush/invalidate 同步到真实 Vulkan 内存。真机探针经设备本地缓冲区往返校验 64 个值通过。
- NX809J Android 16 Guest 中已安装的 Geekbench 6.7.1 完成 Vulkan GPU 测试，得分 **14,578**，设备为 **Adreno (TM) 750**。应用本地历史将整轮标记为完整、有效，8 个工作负载均有效且有分。配对宿主日志记录了 Adreno 上的实际计算派发与读回。
- 成绩来自带 Vulkan 调用跟踪的调试宿主和可回退的 Guest 运行副本。客户端 SHA-256 为 `45d0c273482b3d7833c5dffdb77f897c70d4ad454a0be4c88575451521c03c97`，宿主后端 SHA-256 为 `4e525bfe29ed8574147cbcc05d335df82352ac3fed9ce37867e88a8f4fe5cbdd`。常规 APK 自动接入和 OpenCL 仍待验证。[重建与回退说明](../native/vulkanbridge/client/EMU_PIPE.md#public-vulkan-abi-and-mapped-buffers)。

#### 常规 APK 自动接入 Vulkan 与 OpenCL（2026-11-18）

- GPU 开启的 NX809J/API 36/ARM64 实例从 APK 启动时，VM 自行启动 Adreno 宿主 renderer、等待私有 socket 就绪，并在启动 Guest Android 之前安装 APK 中的客户端库。每个 Guest runner 进程使用独立的 GLProcessPipe 及资源身份。退出时先关闭属于该进程的渲染流，等待其工作线程退出，再关闭进程管道。原始固件保留，运行副本的库和 OpenCL 公共声明有备份。[构建与回退](../native/vulkanbridge/client/EMU_PIPE.md#normal-apk-with-the-gpu-bridge)。
- OpenCL 使用 clvk 和同进程 clspv，将实时编译的内核通过这条 Vulkan 通路送至 Adreno。工作组归约探针在 Guest 对 64 个输入得到正确结果。首次运行 Geekbench OpenCL 时，clspv 的 8 位向量降级生成了无效的 `OpConvertUToF`。补齐线协议的 FP16/INT8 结构协商后，真机 Guest 的扩展和 Vulkan 1.2 核心结构查询均与宿主一致。clvk 随后按核心版本与实际特性位识别已纳入 Vulkan 核心的 8 位及 16 位存储能力，继续执行原有 SPIR-V 验证。独立的 `uchar4 -> float4` 编译对照复现了降级失败，并验证使用 INT8 的 SPIR-V。
- OpenCL 在 Edge Detection 曾卡在宿主驱动销毁命令池。为跨线程的命令池销毁和命令缓冲释放加入同一传输通道的完成确认后，宿主退出了该调用，随后暴露出 clvk 在只读映射解除时对已失效的 `CL_MEM_USE_HOST_PTR` 进行回写。clvk 现在仅回写以写权限映射的数据。更新后的 Release APK 经正常启动完成 OpenCL 全测，应用本地结果为 **4,239**，Adreno 750，8 个工作负载均有效。
- 同一已签名 Release APK 随后完成 Geekbench 6.7.1 Vulkan 全测，得分 **15,047**，Adreno 750，8 项均有效。两次结果均由应用本地数据库确认为完整、有效。在手机上核对了安装 APK 与 `build/diagnostics/rme-geekbench-cached-host/RedMagicEMU-GPU.apk` 的 SHA-256 `470252ff516e091d40b3b40f2ff830bb0745e1dd934bdadbfa91bf516ebe07af` 一致。APK 内的 7 个宿主文件、6 个 Guest 文件及 14 份上游许可文件已核对。
- Release 构建、VM 与运行库定向单元测试、AndroidPipe、公开 Vulkan ABI 和 RenderControl 协商测试通过。Windows 全量 JVM 测试仍有既有的 `InstanceRuntimeLeaseTest.anotherProcessBlocksAndDeathReleasesLock` 文件锁用例失败。真机验收范围为 NX809J Guest 与本次 Adreno 750 上的 GPU 计算。

#### 原生 OpenCL 宿主桥 Windows 验证（2026-11-18）

- 同一 C 调度器分别在测试客户端进程内运行和通过匿名管道交给独立宿主进程运行。Windows OpenCL ICD 直接选择 RTX 3080（驱动 617.14），由原生 OpenCL 驱动编译和执行内核。每个样本逐字校验输出，另验证编译失败日志、失败后继续执行上一份内核及释放缓冲区后的错误码。
- 四组工作负载各测 300 次。65,536 个工作项各迭代 32,768 次时，合并提交和读回的管道路径从直调的 2,075.7 微秒增至 2,131.7 微秒（+2.7%），原生 GPU 事件耗时分别为 1,776.1 和 1,760.3 微秒。读回 256 KiB 的较短负载使用共享内存回复时，从直调的 286.7 微秒增至 321.8 微秒（+12.2%），同一负载用管道回传为 339.6 微秒（+18.5%）。64 项小任务合并提交和读回为 61.1 微秒，直调为 54.5 微秒。这些时间包含同步和读回，编译和首次上传单列在计时范围之外。
- 同一验证程序也在 AMD `gfx1036` 驱动 3652.0 上通过四组负载的编译和结果校验，每组各测 50 次。已构建同源 Android ARM64 调度库，接入 Android 宿主和 Guest 公开 OpenCL ABI 后仍需在 NX809J 上重跑 Geekbench 6。[构建、协议及复测](../native/openclbridge/README.md)。

#### 常规 APK 接入原生 OpenCL（2026-11-18）

- 常规 GPU 启动流程安装公开 `libOpenCL.so`。带原生 OpenCL 标记的 APK 同时从 `:vm` 启动 `libaemuclhost.so`，通过实例私有的 `/dev/socket/aemucl` 将 OpenCL C 和计算命令送至宿主 `libOpenCL.so`。服务复用 Vulkan 的 Guest runner、进程祖先和根目录身份检查，用 pidfd 绑定来宾进程。宿主没有可用驱动时，Guest OpenCL 枚举返回无平台，GPU 渲染继续沿用原有流程。
- WSL 的公开 ABI 测试程序通过本机匿名管道使用 Windows NVIDIA 原生 OpenCL 驱动，识别 RTX 3080。逐元素计算、工作组归约、2D 图像采样、带行间距传输、矩形读回、跨队列事件、`CL_MEM_USE_HOST_PTR` 映射和回写、源码编译与链接、二进制程序重载均通过。故意错误的内核和预期输出分别触发真实构建日志和结果校验失败。本地 Geekbench 6 二进制中的 81 个 OpenCL 函数名全部可解析，用户事件等尚无跨进程实现。
- Android ARM64 客户端和宿主库、Release APK 构建及 VM 定向 JVM 测试通过。APK 的 Vulkan 和 GLES 载荷与上次实测 APK 逐字节一致，签名证书也相同。产物 `build/diagnostics/opencl-native/RedMagicEMU-NativeOpenCL.apk`，SHA-256 `56e25ac1a6a029c7f811a73de9245166ea07be2c8569553798e640bbcaf6a1ce`。Android 安装、常规启动和 Adreno 上的 Geekbench 6 分数待手机复测。[重建与回退](../native/openclbridge/README.md#regular-android-apk)。

#### Adreno 原生 OpenCL 实机验收与传输剖析（2026-11-18）

- 常规启动上述签名 APK。宿主 `:vm` 同时映射桥接库与厂商 `libOpenCL_adreno.so`，Geekbench 进程连接实例私有 OpenCL socket。NX809J Guest 的 Geekbench 6.7.1 OpenCL **10,096**，用时 100.47 秒，设备为 QUALCOMM Adreno 750，8 项全有效。相同 APK 的 Vulkan **15,421**，用时 182.00 秒，同一 GPU，8 项全有效。
- 关闭 Guest 后，将实例中同版本 APK 通过宿主包管理器安装，原生 Geekbench 6.7.1 OpenCL **14,535**，用时 80.00 秒，8 项全有效。Guest 总分为宿主的 69.5%，Background Blur 为 28.2%，Face Detection 为 53.6%，Particle Physics 为 55.3%。
- 带 `-Profile` 的 Guest 对照跑得 **9,731**，8 项全有效。单一进程连接中缓冲区上传 7.07 GB、读回 4.10 GB，宿主读写这两类 socket 载荷累计约 5.05 秒。记录了 28,305 次等待和 29,796 次释放，等待包含驱动及真正的 GPU 执行时间。
- 关闭 Guest 后在同一宿主运行原生 Vulkan，Geekbench 6.7.1 **16,286**，136.47 秒，8 项全有效。未调优 Guest Vulkan 总分为该次宿主的 94.7%。
- 将至少 64 KiB 的 OpenCL 读写放到宿主创建的共享 memfd，在小命令上合并消息段。WSL 测试逐字核对 256 KiB 和 2 MiB 数据，Windows 原生 RTX 3080 路径同时覆盖没有共享映射的模式。安装到真机后，核实 Geekbench 和宿主进程同时映射了共享 memfd。共享内存版实机 OpenCL **9,936**，98.75 秒，8 项全有效。相对 **10,096** 的首次结果，整轮用时略缩短，分数未提高。
- Vulkan Guest 编码器开启 `-O2`，重链保持既有 Vulkan 宿主库、传输、前端和清理同步。ARM64 `libvulkan.so` 从 35,527,880 缩至 16,951,536 字节，公开符号、SONAME、动态依赖和 16 KiB 对齐一致。PC 上相同生成的 `vkCmdDispatch` 编码体七组中位数从 319.614 降至 77.457 ns，采样线数据和校验值一致。这一计时涵盖 CPU 编码。[构建入口](../native/vulkanbridge/client/rebuild_encoder_candidates.py)。
- 调优 APK `build/diagnostics/opencl-native/RedMagicEMU-GPU-Tuned.apk` 的 SHA-256 为 `cb6caabf458861a765bb8610ba39b2f27ed09abd6f5549ca0b7efa78168876f7`，安装后核对一致。该版 Vulkan **15,263**，193.04 秒，8 项全有效。本次整轮分数较此前低 1.0%，CPU 编码对照的提速尚未反映在 Geekbench 总分上。
- 在共享内存版本上，本地维护 OpenCL 事件引用，最后一次释放通过有序单向命令交给宿主。WSL 和 Windows RTX 3080 公共 ABI 测试通过，覆盖引用计数、回调、释放后拒绝旧句柄、跨队列依赖和程序二进制。相同调优 APK 在 NX809J Guest 的 OpenCL 为 **10,459**，96.65 秒，8 项全有效。较首轮 10,096 高 3.6%，为本机原生 14,535 的 72.0%。

#### OpenCL 事件释放随请求发送（2026-11-18）

- 宿主声明支持后，Guest 将一个已释放的事件 ID 附在下一条请求中，宿主先完成该释放再处理新请求。连续释放时单独发出前一个 ID，连接退出时回收剩余事件。新 Guest 配前版宿主的公开 ABI 测试通过。
- RTX 3080 上 512 轮事件提交、等待、释放的真实驱动对照中，独立释放请求由 512 降至 0，请求合计由 1,544 降至 1,032（−33.2%），事件和常规模块测试通过。WSL 共享内存对照同时通过。
- 签名包 `build/diagnostics/opencl-native/RedMagicEMU-GPU-InlineRelease.apk` 的 SHA-256 为 `e34176ff351a9b54fdce9aebfff650dd3c81754de6398059a45abdaa3fd9818c`。核对安装 APK 和 Guest 库后，实机 OpenCL 得 **10,413**，96.97 秒，8 项全有效。前版为 **10,459**、96.65 秒。本轮请求数下降，Geekbench 单次总分没有提高。

#### OpenCL 线性缓冲区异步上传（2026-11-18）

- 线性缓冲区的非阻塞写提交给驱动时保留 `CL_FALSE`。宿主为传输保留独立数据，按原生事件完成回调回收，单连接暂存上限为 128 MiB。暂存空间用尽时当前写使用同步路径。
- WSL 的真驱动延迟对照把两个 512 KiB 写挂在宿主测试事件后面，直到 Guest 发出 `clFinish` 才放行。旧桥在首个请求因传给驱动 `CL_TRUE` 被拒绝。新桥在共享映射被复用后仍逐字节读回两段不同数据，且覆盖依赖事件与无返回事件的上传。Windows RTX 3080 的公共 ABI、数据回读和事件请求对照同时通过。
- 签名 APK `build/diagnostics/opencl-native/RedMagicEMU-GPU-AsyncUpload.apk` 的 SHA-256 为 `41021c8b9aff4d327b6fced5d4ebe205f5a8b6bfd1a39406ec0aca7d1239fb61`，安装后核对一致。Guest Geekbench 6.7.1 OpenCL **10,435**，97.11 秒，8 项全有效。上版 **10,413**、96.97 秒。此轮恢复了异步上传路径，单次跑分对照未显示明显收益。

#### Adreno OpenCL 跨进程缓冲区对照（2026-11-18）

- 手机宿主报告 SM8650（骁龙 8 Gen 3）、Adreno 750。驱动通告 `cl_qcom_android_ahardwarebuffer_host_ptr` 和 `cl_qcom_dmabuf_host_ptr`。探针实测前者，在 OpenCL 进程导入两个 4 MiB AHardwareBuffer BLOB，另一个进程通过 Unix socket 接收缓存型 DMA-BUF，使用 `mmap` 和 `DMA_BUF_IOCTL_SYNC` 读写。每项实验各跑三轮，每轮每种路径 24 个计时样本，逐字匹配 CPU 参考值。故意改动预期值的对照在普通映射和 AHardwareBuffer 两种模式下都退出 1。
- 普通 memfd 的 `CL_MEM_USE_HOST_PTR` 映射模式中，数据就绪时间约为同轮读写基线的 2.2–3.0 倍。AHardwareBuffer 模式中，粒子类合成负载的就绪时间在三轮内下降 46–52%，模糊类合成负载下降 1–48%。GPU 内核事件主要落在 143–160 微秒，收益来自数据传输和同步路径。每轮的原始汇总保存在 `build/diagnostics/opencl-native/shared-buffer-{map,ahb}-adreno.txt`。
- 以上为两个宿主进程的对照。源码、Android ARM64 构建和 16 KiB 对齐检查见[探针说明](../native/openclbridge/README.md#adreno-shared-buffer-probe)。

#### NX809J N64 Guest 上的 Adreno 共享缓冲区对照（2026-11-18）

- 用已安装 APK 的原有 API 36 N64 runner 启动临时 Guest 计算探针的 CPU 端。宿主通过私有 Unix 流发送 memfd 基线和 Android Hardware Buffer 的数据 DMA-BUF。Guest 使用现有 runner 的 `recvmsg`、`mmap` 和 `DMA_BUF_IOCTL_SYNC` 完成读写。256 KiB、1 MiB、4 MiB 各三轮，逐字校验均通过，故意错一字的负对照各退出 1。探针以实例所有者 UID 从 ADB 临时启动，覆盖 N64 数据路径。APK 的普通应用和正式 OpenCL socket 尚未使用该分配方式。
- 数据就绪时间按同轮普通读写计，模糊类负载在 1 MiB 的三轮慢 12–31%，在 4 MiB 的三轮慢 3–22%。256 KiB 模糊负载和三种尺寸的粒子类负载均有波动。另以相同二进制交替运行三个宿主/Guest 对照，4 MiB 两边都出现加速及减速。GPU 内核事件耗时接近，直接共享也需要 CPU 缓存同步。当前结果不足以支持启用该路径。
- NDK ARM64 构建及 16 KiB 段对齐检查通过，手机上的临时二进制已移除。日志位于 `build/diagnostics/opencl-native/shared-buffer-n64-*-adreno.txt` 及 `shared-buffer-n64-1024-paired.txt`。本轮产物为探针和对照记录，未安装新 APK 或生成新的 Geekbench 分数。

#### OpenCL FP16 能力通告对照（2026-11-18）

- 旧桥返回空的设备扩展列表，同一查询通道从 Adreno 750 读到 `CL_DEVICE_HALF_FP_CONFIG=0x16`。对照桥只在宿主设备实际通告 `cl_khr_fp16` 时返回该扩展名。标准 OpenCL 客户端按返回值选择编译半精度内核，检查该扩展名的长度查询以及过小缓冲区的返回码。[Khronos FP16 扩展](https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_API.html#cl_khr_fp16)。
- 实机独立 socket 对照以及 NX809J API 36 N64 运行器均通过 `half4` 缓冲区读写和算术验证，各核对 16 个半精度结果。Windows RTX 3080 的原生驱动未通告该扩展，桥上负对照及常规公开 ABI 套件通过。另有一条带向量常量的 `convert_half4` 内核会触发 Adreno 编译器的内部类型检查错误，原生直连以状态 134 退出。该用例记录为厂商驱动限制。
- 对照 APK `build/diagnostics/opencl-native/RedMagicEMU-GPU-FP16.apk` 通过签名校验且安装后 SHA-256 确认为 `e2177daaa47979d676db105a580fd15635cdf56d51b96ba5db8a3c2a55905138`。与异步上传基线的内容对照仅改变 OpenCL 宿主库和签名条目。
- 常规启动的 NX809J Guest 在 Geekbench 6.7.1 OpenCL 实测 **12,570**，91.40 秒，Adreno 750，8 项全有效。上一版为 **10,435**、97.11 秒，本次总分提高 20.5%，达到同一手机原生 **14,535** 的 86.5%。Background Blur 从 2,261 到 3,895，Particle Physics 从 27,604 到 47,902。Gaussian Blur、Feature Matching、Stereo Matching 的单次得分略低。成绩从 Guest 本地数据库核对，另保留结果页和逐项记录。Vulkan 沿用同一已验收载荷。

#### OpenCL 缓冲区图像视图对照（2026-11-18）

- 按同一手机的逐项结果，FP16 Guest 的 Background Blur 为原生的 66.7%，Particle Physics 为 74.7%。驱动通告 `cl_khr_image2d_from_buffer`，两个对齐查询均返回 64 像素。现有桥可传递图像关联的缓冲区句柄，但吞掉了扩展名，两个查询都返回 `CL_INVALID_VALUE`。[扩展规范](https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_API.html#cl_khr_image2d_from_buffer)。
- 临时桥验证转发扩展名和对齐值后，可以创建关联原缓冲区的只读采样路径。在 1024×1024 RGBA 的五点模糊实验中，比较预先写入的数据通过独立图像拷贝和直接图像视图。NX809J N64 Guest 各轮交替测量 25 个样本，两条路径均逐字匹配 CPU 输出。省掉一次显式缓冲区到图像拷贝后，三轮中位就绪时间分别由 2,726.4、2,374.9、2,589.9 微秒降为 2,036.8、2,201.6、2,199.7 微秒。驱动的模糊内核事件接近，单次拷贝的 GPU 事件约 142–144 微秒。独立宿主进程对照三轮均加速，额外一次宿主 IPC 对照略慢，记录了不同运行条件下的波动。
- 这一轮验证使用 `build/diagnostics/opencl-native/abi_host-image2d.c` 和临时探针。N64 日志在 `build/diagnostics/opencl-native/image2d-n64/round-*.txt`。常规 APK 的接入和 Geekbench 使用情况仍待整轮对照。

#### OpenCL 缓冲区图像视图的常规 APK 对照（2026-11-18）

- OpenCL 宿主将 `cl_khr_image2d_from_buffer` 加入原生支持时可通告的扩展，转发驱动的行距及基地址对齐查询。图像描述和句柄关联沿用现有 ABI。Adreno 750 上，旧桥的公开接口测试因缺扩展名退出。新桥使用 64 像素对齐构造带填充的图像，关联句柄一致，32 个 RGBA 像素逐字匹配。FP16 实机正对照与 RTX 3080 的扩展负对照、公开 ABI 回归通过。
- 已安装并比对签名包 `build/diagnostics/opencl-native/RedMagicEMU-GPU-Image2D.apk`，SHA-256 为 `b71112ef5202281f8eda1cb0d7b60b86ab293fc4cabdc0c913606583331b0663`。与已实测的 FP16 包相比，变动的载荷为 OpenCL 宿主库。实例通过正常 APK 启动入口启动。
- 图像视图候选在 Geekbench 6.7.1 OpenCL 得 **10,964**，96.34 秒，Adreno 750，8 项全有效。相比 FP16 包前一次的 **12,570** 下降 12.8%。Background Blur 从 3,895 到 2,007，Face Detection 从 3,000 到 1,984，Particle Physics 从 47,902 到 49,079。尚未记录到 Geekbench 实际使用图像视图的调用证据。
- 恢复已签名且验过哈希的 FP16 APK，以及相同的默认桥源码，重建的两个 OpenCL 库与原 FP16 库逐字节一致。恢复后重新正常启动并跑得 **12,291**，92.86 秒，8 项全有效，比候选高 12.1%。按本次对照保留 FP16 路径。候选的改动源码存档于 `build/diagnostics/opencl-native/image2d-candidate-source`，结果为 `adreno-guest-image2d.json` 和 `adreno-guest-image2d-fp16-recheck.json`。

#### OpenCL 非阻塞读回复现（2026-11-18）

- Guest 的 `clEnqueueReadBuffer(CL_FALSE)` 已在线协议中传递非阻塞标志。宿主 `READ_BUFFER` 固定以 `CL_TRUE` 提交，待驱动写入本次回复缓冲区后才返回。较早的 Geekbench 剖析记录 660 次线性读回，其中 440 次请求非阻塞。这一计数来自 FP16 前的测试包。
- 在同一台 Adreno 750 上，给一次 256 字节读回添加一个约 400 毫秒后放行的原生 OpenCL 用户事件。直接调用 `CL_FALSE` 在 0.02 毫秒返回，事件起初为 `CL_QUEUED`，数据约 402 毫秒后就绪。NX809J N64 Guest 经私有试验桥调用同一 API 时，驱动收到 `CL_TRUE`，调用约 403 毫秒后才返回，事件已完成。两边都核对了 64 个输出值。日志在 `build/diagnostics/opencl-native/async-read-repro`。
- 本轮为可重跑的复现。恢复非阻塞语义需要让独立结果缓冲区一直活到读回完成，并在 Guest 观察到完成状态之前写入调用者的内存。[OpenCL 读回规范](https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_API.html#clEnqueueReadBuffer)。

#### OpenCL 线性缓冲区非阻塞读回（2026-11-18）

- 配对桥声明支持后，宿主以 `CL_FALSE` 提交，每笔读回持有独立暂存及原生事件。完成数据在下一次 API 回复前送到 Guest，应用观察到事件完成或排空队列前已写入应用的目标内存。宿主保留单连接 128 MiB 的未交付读回暂存。大结果可复用 Guest 已确认的共享映射。旧宿主使用原来的协议。
- Adreno 测试驱动将两笔 512 KiB 读回挂起，旧版在提交时因 `CL_TRUE` 失败。修复后事件依赖、无返回事件的读回、状态轮询、`clWaitForEvents` 和 `clFinish` 全部逐字节通过。强制让 Guest 的共享内存 `mmap` 失败，仍通过流式传输拿到正确结果。N64 上同一个定时对照由 **403.09 毫秒**阻塞提交缩短到 **0.58 毫秒**，等 GPU 放行约 **415.14 毫秒**后 64 个值正确。RTX 3080 的公开 ABI 套件和新 Guest 配旧宿主的测试通过。
- 最终签名包为 `build/diagnostics/opencl-native/RedMagicEMU-GPU-AsyncRead.apk`，SHA-256 `7e7cff7e95ebe615df41a5d6f362ba70b5b14eec550a9cb00c0cdaaea771a0ac`。OpenCL 客户端与宿主库从同次 Android ARM64 构建替换，Guest 资源索引已按新哈希重建。回退使用 `RedMagicEMU-GPU-FP16.apk` 覆盖安装后按正常流程重启 Guest。
- 安装最终签名包并通过常规入口启动，核对安装包、Guest 客户端库以及宿主加载的厂商驱动。Geekbench 6.7.1 OpenCL 实测 **11,825**，92.76 秒，Adreno 750，8 项全有效。此前 FP16 复测 **12,291**、92.86 秒，本轮确认读回语义修复，单次总分低 3.8%。

#### OpenCL 异步读回直接写入共享槽的对照（2026-11-18）

- 现有 AsyncRead 先让驱动写入独立的宿主暂存，然后通过共享传输区或 socket 把结果交给 Guest。对照让 Adreno 的 `clEnqueueReadBuffer(CL_FALSE)` 直接把两笔同时挂起的读回写进专用 memfd 的两个槽。两条路径用相同的 N64 Guest 消费端，写入目标缓冲后逐字节校验。每种尺寸各三轮，每轮每条路径 24 个样本。
- 两笔各 4 MiB 的读回，从提交到宿主数据就绪的中位时间由 8.07、9.01、8.39 毫秒降至 6.68、8.02、7.16 毫秒，下降 11.0–17.2%。原路径两笔结果的宿主拷贝合计约 1.44–1.77 毫秒。对照的暂存路径将两笔都送入共享内存，单独比较这一层拷贝。时间包含等待驱动完成与宿主拷贝，分配、Guest 通知和结果校验在计时之外。
- 源码、运行脚本和三轮输出位于 `build/diagnostics/opencl-native/readback-slot-probe`。手机临时文件已移除。本轮测试独立共享槽，尚未接入常规 APK，Geekbench 收益仍待验证。

#### 常规 APK 接入 OpenCL 异步读回共享槽（2026-11-18）

- 支持的 socket 宿主按连接分配大小固定的 memfd，包含四个 16 MiB 的独立槽。64 KiB 至 16 MiB 的异步读回可以让 Adreno 直接写入空槽。Guest 收到完成通知后直接复制到应用缓冲区，省掉宿主中间拷贝。槽随原生事件和有序回复回收，超尺寸、满槽、无共享映射及未提供此能力的宿主继续用已有路径。
- 旧桥在驱动目的地址检查中失败。新桥发送五笔带不同数据的挂起读回，驱动前四笔直接写共享槽，第五笔回退，事件完成后全部逐字节校验通过。强制让两类 Guest 映射都失败，socket 回退通过。原有 N64 运行器的真驱动延迟测试和 Windows RTX 3080 公开 ABI 测试通过。
- 签名包 `build/diagnostics/opencl-native/RedMagicEMU-GPU-ReadbackSlots.apk`，SHA-256 `9b9c1dd27b0ee1d954cab222b81ed51a9fa9c7c25f0c1f0e3bad9d2444b8f024`。OpenCL 客户端与宿主库以及配对 Guest 索引从同一次构建替换。回退使用 `RedMagicEMU-GPU-AsyncRead.apk` 覆盖安装后按常规流程重启 Guest。
- 安装包和 Guest 库哈希通过核对。常规启动后，Geekbench 6.7.1 OpenCL 在 Adreno 750 实测 **12,202**，90.71 秒，8 项全有效，较上版 **11,825** 高 3.2%。宿主和 Geekbench Guest 进程同时映射同一个 `rme-cl-readback` memfd，分别为可写和只读。该分数来自单次整轮对照。

#### OpenCL 非阻塞图像上传复现（2026-11-18）

- 在同一台 Adreno 750 上给 `clEnqueueWriteImage(CL_FALSE)` 加一个约 400 毫秒后放行的依赖。直接调用厂商驱动，提交耗时 **0.020 毫秒**，返回时事件处于 `CL_QUEUED`。用当前源码构建 Guest 客户端和宿主测试程序，经独立 socket 调用时，厂商驱动收到 `CL_TRUE`，提交耗时 **404.470 毫秒**，返回时事件已完成。两边均通过 16 像素校验，带行间距的输出缓冲区填充字节保持原值。
- 复现代码、脚本和日志位于 `build/diagnostics/opencl-native/async-image-upload-repro`。本轮使用独立测试程序，未更新常规 APK 或重跑 Geekbench。

#### 常规 APK 接入 OpenCL 非阻塞图像上传（2026-11-18）

- `clEnqueueWriteImage(CL_FALSE)` 将非阻塞标志传给宿主，宿主以 `CL_FALSE` 提交已经按行打包的像素，并在原生事件完成前保留暂存。调用者不取回事件时，宿主仍获取内部事件以回收暂存。与线性写入共用 128 MiB 的上传额度。图像上传暂存不足时返回 `CL_OUT_OF_HOST_MEMORY`。旧宿主继续按同步图像上传处理请求。
- Adreno 750 回归测试让两笔 128 KiB 图像上传保持挂起，第二笔依赖第一笔且不取回事件。旧桥因向驱动传入 `CL_TRUE` 失败。修复后，Adreno 独立桥和 NX809J N64 Guest 均通过像素、行填充和线性上传校验。400 毫秒依赖对照中，独立桥的提交耗时由 **404.470 毫秒**降到 **0.350 毫秒**，返回时事件仍在排队，约 **404.095 毫秒**后数据就绪。Windows RTX 3080 的公开 ABI 套件通过。
- 以 ReadbackSlots APK 为基底，替换配对宿主和 Guest 库并重建 Guest 索引。签名包 `build/diagnostics/opencl-native/RedMagicEMU-GPU-AsyncImageUpload.apk`，SHA-256 `31f869263d543355658b05d37294bfafd48e82bfb79635fc422c694b682c0a77`。核对了签名、对齐、504 个非签名条目、手机安装 APK 和实际加载路径中的 Guest 库。回退使用 `RedMagicEMU-GPU-ReadbackSlots.apk` 覆盖安装，再按常规流程启动 Guest。
- 常规入口启动 NX809J Guest 后，Geekbench 6.7.1 OpenCL 在 Adreno 750 首轮 **11,375**（93.64 秒），复测 **12,777**（89.99 秒），两轮各 8 项均有效。上一版记录为 **12,202**。首轮分差主要集中在 Background Blur 和 Face Detection。这两轮确认功能通过，尚无法从分数波动确定修复对总分的影响。使用当前桥另跑 N64 FP16 测试通过，16 个结果正确，半精度配置为 `0x16`。

#### 常规 APK 接入 OpenCL 非阻塞矩形缓冲区上传（2026-11-18）

- `clEnqueueWriteBufferRect(CL_FALSE)` 保留非阻塞标志。宿主沿用线性缓冲区和图像上传的暂存与事件回收机制。矩形数据按原有协议紧密打包，目标的行距、层距和偏移传给原生驱动。暂存不足时返回 `CL_OUT_OF_HOST_MEMORY`。旧宿主仍使用原有同步路径。
- Adreno 用原生事件把两笔各 128 KiB 的写入挂起到 `clFinish`。旧版因传入 `CL_TRUE` 被测试驱动拒绝。修复后，独立桥和 NX809J N64 Guest 均通过。源和目标采用不同的行距和层距，两笔写入的偏移也不同。第二笔等待第一笔且不取回事件。测试逐字节检查整个目标缓冲区，包含写入区域外的内容。RTX 3080 公开 ABI 套件和 N64 Adreno FP16 内核也通过。
- 签名包 `build/diagnostics/opencl-native/RedMagicEMU-GPU-AsyncRectUpload.apk`，SHA-256 `157245225149c564a11bea10e5e11faffc7d3567588aece3c23b405953f8023f`。以 AsyncImageUpload 为基底替换成对宿主库、Guest 库和 Guest 索引，并校验 APK 签名、对齐和全部 504 个非签名条目。回退时覆盖安装 `RedMagicEMU-GPU-AsyncImageUpload.apk` 后重启 Guest。
- 核对手机上的安装包和运行时 Guest 库哈希。常规入口的 Adreno 750 Geekbench 6.7.1 OpenCL 首轮 **11,128**（93.89 秒），复测 **12,326**（90.69 秒），两轮各 8 项均有效。上一版两轮分别为 **11,375**、**12,777**，本轮没有测到总分提升。用当前 Guest 配上版 Windows 宿主运行公开 ABI 套件也通过。
