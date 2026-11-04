# S0 基线审计

记录时间：2026-11-04。来源为 https://github.com/uxazu/AEmulator，取得时 HEAD：`c3e94275a83dbab0bdc0d7dca75c2431c27fe1f9`；阶段分支 `s0-baseline-audit`。初始工作树干净。本次新建 checkout 无法恢复已删除项目及其历史。

## 代码入口

- `app/src/main/AndroidManifest.xml`：`MainActivity` 的启动、VIEW 和 BOOT 入口；`VmActivity`、`Keeper` 在 `:vm` 进程。
- `app/src/main/java/app/aemu/ui/LibraryModel.kt` 调用 `importer/Importer.kt`；导入中 `Analyzer.kt` 分析属性和 init 并生成计划及镜像信息，`core/TreeFixer.kt` 做清理。`core/GuestImage.kt` 表示镜像，`core/InitPlan.kt` 解析计划。
- `ui/VmActivity.kt` 创建 `core/GuestVm.kt`；启动期间执行 TreeFixer、选择 `core/Kernel64.kt` 配置并由 `core/GuestRunner.kt` 启动服务。以上是静态代码检查，非启动验证。
- `core/ImageStore.kt` 管理镜像、克隆和数据重置；`Importer.kt` 有 TWRP 备份导入处理。检索未发现完整的备份导出和恢复入口，不能断言绝不存在；Manifest 设置 `allowBackup=false`。

## 工具链与构建

上游 README 指定 JDK 17、Android SDK 36、NDK r28；`gradle/wrapper/gradle-wrapper.properties` 指定 Gradle 8.14.2，根构建脚本指定 Android Gradle Plugin 8.13.2、Kotlin 2.2.21。`app/build.gradle.kts` 设置 compileSdk 36、Java/Kotlin 目标 17、arm64-v8a；`gradle.properties` 指向本机不存在的 Android Studio JBR 路径。`native/*/build.sh` 单独构建原生部分，不能假定 APK 构建会重建所有原生产物。

本机 PATH 的 Java/Javac 为 25.0.4，另发现 JDK 18 和 21；未设置常见 Android SDK/NDK 环境变量，常见 SDK 路径不存在，不能据此证明其他位置无安装。

实际执行：

- `git status --short --branch`、`git rev-parse HEAD`、`git remote -v`：确认初始干净分支、上述提交和来源。
- `java -version`、`javac -version`：均为 25.0.4。
- `./gradlew.bat :app:assembleStandardDebug --no-daemon`：下载 wrapper 指定的 Gradle 8.14.2 后失败，项目指定的 Android Studio JBR 不存在。
- `./gradlew.bat :app:assembleStandardDebug --no-daemon '-Dorg.gradle.java.home=C:\Program Files\Java\jdk-21.0.10'`：退出码 1；配置编译任务依赖时报告找不到 Android SDK。临时参数没有修改项目配置。未完成编译，原生构建也未验证。

随后按用户要求使用本机 JDK 21.0.10，安装 Google Android 命令行工具及 SDK 到 `<用户目录>/.redmagicemu-tools/android-sdk`：Android 36 平台修订版 2、Build Tools 35.0.0 和 36.0.0、platform-tools、NDK 28.2.13676358（r28c）。命令行工具下载包 SHA-256 与 Google 公布值一致；项目根目录被忽略的 `local.properties` 指向此 SDK，不修改机器全局环境变量。README 原要求 JDK 17，此处是用户指定的 JDK 21 基线。

PowerShell 中设置 `$env:JAVA_HOME='C:\Program Files\Java\jdk-21.0.10'` 和 `$env:ANDROID_HOME='<用户目录>\.redmagicemu-tools\android-sdk'`，然后运行 `./gradlew.bat :app:assembleStandardDebug --no-daemon '-Dorg.gradle.java.home=C:\Program Files\Java\jdk-21.0.10'`，退出码 0，`BUILD SUCCESSFUL`，40 项任务执行。输出为 `app/build/outputs/apk/standard/debug/app-standard-debug.apk`（92,313,038 字节）。该命令覆盖项目中无效的 JBR 路径；输出和 `local.properties` 都不纳入 Git。

构建出现 SDK XML 版本、Manifest `extractNativeLibs`、Kotlin 弃用 API 警告；部分预编译 `.so` 无法剥离符号，按原样打包。APK 构建未证明这些原生组件可以从源码重建；独立原生构建未执行，其脚本可能覆盖仓库中的预编译文件。安装、真机启动和功能测试未执行。

## 源码、资产和改名风险

`native/guest64/36`、`native/hostjni`、`native/glbridge` 等有源码与构建脚本；`app/src/main/jniLibs/arm64-v8a` 和 `app/src/main/assets/engines/n64-36` 含预编译程序、库与资产。`third_party/payload-dumper-go/README` 和 `third_party/7-zip/README` 描述外部发布的二进制。不能仅凭存在源码目录认定全部依赖可重建，需逐项追踪版本、来源和许可。

当前 namespace 和标准版 applicationId 为 `app.aemu`；更改应用 ID 会改变 Android 应用身份和私有数据位置，应设计迁移并验证。Manifest 的 provider authority 使用 `${applicationId}`，BOOT action 固定为 `app.aemu.BOOT`；JNI 包含 `Java_app_aemu_core_*` 符号，内部包名迁移可能破坏绑定。裁剪须先追踪 Kernel64、TreeFixer 和运行时引用，再做设备回归；保留上游版权、许可证与来源说明。

## 诊断证据与限制

仓库已跟踪四份历史 `hs_err` JVM 崩溃日志及一份 replay 日志，前者记录历史原生内存分配失败及本地环境信息；它们不是 Guest 启动日志，无法说明当前构建失败或 Guest 根因。忽略规则不影响已跟踪文件，须注意现有历史资料的隐私风险；本轮未删除或改写它们。

2026-11-04 审阅用户提供的历史导出日志。用户说明它来自过去修改过的 AE 版本，不能视为当前 checkout 的实测；原文件保留在用户下载目录，不纳入仓库。日志标签显示 NX809J、Android 16/API 36 和 ARM64 引擎，但标签不能证明固件的唯一身份。

- 可观察事实：Guest main 日志记录 health 服务进入 charger mode；宿主记录 ztecharger 启动后出现 `guest requested power off (unplug)`。日志还记录一些 vendor 服务退出或 HAL 故障。Guest 日志只保留末尾 4 MiB，不能由缺少某条启动记录断定该事件从未发生。
- 待验证假设：充电/关机路径可能与此次提前停止有关；触发者、因果关系及与 HAL 报错的关系都未确定，需核对当时版本并在当前代码上复现。不能把所有 HAL 报错当作根因。
- 历史分析：日志来自旧的修改版，既不能证明当前代码行为，也不能恢复已删除对话中的分析。上游 README 的功能描述是上游声明，不是 NX809J 实测。固件校验、安装、当前版本真机启动和功能均未验证。
