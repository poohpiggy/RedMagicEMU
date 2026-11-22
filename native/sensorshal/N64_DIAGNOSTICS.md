# N64 运动传感器链路检查

范围为 REDMAGIC NX809J、Android 16/API 36、ARM64 客体及 N64 运行环境。以下首轮诊断未修改原始固件或客体运行时文件。

## 本次客体观察

- 客体的 sensorservice 可找到，但报告没有传感器。
- HIDL Sensors 服务已注册，但进程映射中未看到 `sensors.default.so`。客体原厂 `/vendor/etc/sensors/hals.conf` 指向 `sensors.qsh.so`。
- 观察到原厂 AIDL 传感器进程，但查询 AIDL `default` 服务未找到实例。

这些观察表明服务存在并不等于预期的加速度计 HAL 已加载；它们尚不足以单独证明加载失败的全部原因。此处不记录原始日志或设备个人信息。

## 仓库与二进制证据

- `app/src/main/assets/engines/n64-36-own/vendor#etc#vintf#manifest.xml` 声明 HIDL `android.hardware.sensors@1.0::ISensors/default`；`app/src/main/assets/engines/n64-36/vendor#etc#init#android.hardware.sensors@1.0-service.rc` 指向 `/vendor/bin/hw/android.hardware.sensors@1.0-service`。`app/src/main/java/app/aemu/core/TreeFixer.kt` 中的 N64 资产分层决定运行树的安装文件；声明和打包本身不证明实际加载。
- `app/src/main/assets/engines/n64-36/vendor#lib64#hw#sensors.default.so` 和对应的 `system#lib64#hw#sensors.default.so` 是相同的 AArch64 ELF。反汇编显示读取 16 字节记录、使用 `CLOCK_MONOTONIC` 在读取时生成事件时间、固定精度为 3。`app/src/main/java/app/aemu/core/Sensors.kt` 的记录只含类型和三轴值，没有宿主采样时间或精度；积压的旧值可能被赋予新时间后送出。
- Git 提交 `3b97ed5` 同时引入 N64-36 库和 `native/sensorshal/sensors_hal.c` 的历史版本。使用 NDK 28.2 将该版本交叉编译为 AArch64 后，代码段长度及大部分机器指令与预编译库吻合，支持二者的来源关联；产物哈希不同，原工具链及完整构建参数未确认，不能声称逐字节重建。当前 `native/sensorshal/build.sh` 构建的是 ARMv7 的 `engines/common/sensors.default.so`，不是 N64 更新产物。
- 已有关联源码中的 `batch` 不调整采样周期；`flush` 仅增加计数，不唤醒阻塞的读取。协议、同步和 HAL 生命周期需要共同验证后才能更改。

## 尚未验证

首轮诊断时，尚未确认原厂 `hals.conf` 与客体运行时 HAL 选择之间的完整调用路径，也未确认如何安全地让服务加载目标库。普通应用的枚举、真实三轴与自然方向映射、宿主和客体时钟关系、时间戳递增、停用后停止、重新订阅、暂停恢复及服务重启后的旧事件处理当时均未通过端到端验证。

## 配置隔离对照与恢复

在正在运行的目标客体副本中，暂时移走 `vendor/etc/sensors/hals.conf` 并重启客体 HIDL 1.0 服务后，进程开始映射模拟器 `vendor/lib64/hw/sensors.default.so`；恢复原配置并再次重启后，该映射消失。原文件的哈希、权限、所有者和修改时间在实验后均与实验前一致。已经启动的 sensorservice 在该对照期间仍报告没有传感器，因而不能据此推断重新启动客体后的应用枚举结果。

`TargetSensorsConfig.prepare` 在目标客体每次准备运行树时，将运行时副本中的 `vendor/etc/sensors/hals.conf` 移到 `system/.aemu-parked/vendor#etc#sensors#hals.conf`；其他目标不处理。原始固件不变，已有备份和源文件同时存在时会报冲突而不覆盖。回滚配置时，应先停止客体，撤销此准备规则，再将运行副本的备份移回原路径；如果原路径已存在，先核实内容，不能覆盖。恢复旧 HAL 时也必须同时回滚主客协议。

## API 36 ARM64 HAL 的可重建产物

`n64_sensors_hal.c` 仅供 `n64-36` 使用，基于本目录的 AEmulator HAL 布局并保留旧实现。使用 Android NDK **28.2.13676358**、API 26 交叉工具链为 ARM64 构建：

```powershell
python native/sensorshal/build_n64.py --ndk C:/Users/<user>/.redmagicemu-tools/android-sdk/ndk/28.2.13676358
```

脚本在临时目录先编译、剥离符号并检查 ELF64、AArch64、SONAME 和 HMI，再更新 `n64-36` 的 `system#lib64#hw#sensors.default.so` 与 `vendor#lib64#hw#sensors.default.so`。它不生成或修改 ARMv7 `common` 资产。静态断言核验 LP64 HAL 字段偏移；实际动态加载仍须在目标 Guest 中验证。

协议使用小端 32 字节 `AEM2` 记录：magic、类型、`CLOCK_BOOTTIME` 采样时间（纳秒）、三轴 m/s² 和精度。按 20 ms 最小周期接收，FIFO 声明为零；HAL 不使用有损压缩或截断三轴。名称、4g 虚拟范围、分辨率和功耗是虚拟接口描述，不代表已查询宿主物理传感器规格。读取端拒绝失效、非递增及超过 500 ms 的样本。`/dev/aemu_sensor_state` 是宿主原子更新的本次会话栅栏：缺失、格式错误或非 active 时不输出事件，暂停/恢复后不输出栅栏前的数据。HAL `flush` 用同步的读边界维护已接收数据与完成事件的顺序；不假称有硬件 FIFO。

本机离线测试（需要 Linux C 编译器）可运行：

```sh
cc -std=c11 -Wall -Wextra -Werror -pthread -o /tmp/rme_n64_test native/sensorshal/tests/n64_hal_test.c && /tmp/rme_n64_test
```

回滚时同时撤销主客 AEM2 协议接线和本次构建的两份 N64 资产；不得只恢复一端。若这两份资产在本轮前没有其他本地改动，可使用 `git restore --source=HEAD -- 'app/src/main/assets/engines/n64-36/system#lib64#hw#sensors.default.so' 'app/src/main/assets/engines/n64-36/vendor#lib64#hw#sensors.default.so'` 恢复 checkout 中的旧库，再重建 APK 并重建或恢复运行时副本。该步骤不会改原始固件。此前“尚未验证”条目是当时的诊断记录，修改后的设备结论以本轮实测为准。

## 2026-11-12 系统侧实机验证

通过无线 ADB 连接已有标准版 `app.redmagicemu`，已安装 APK 的 SHA-256 与本机当时的 standard Debug APK 一致（`5c07841cba7db9e8d788d766ccd037ff5fcb0a61e8fde9efacb57c4a25ad4358`）；没有重新构建或安装。启动唯一已有的 NX809J/API 36/ARM64/N64 实例，日志报告 Guest 启动完成。运行副本中的原厂 `hals.conf` 已移至既定备份位置；HIDL 1.0 服务进程实际映射 `vendor/lib64/hw/sensors.default.so`，其 SHA-256 与工作区 N64 vendor 资产一致（`58ab4df04bf890465ab63ad50ada516afd967c10848c6393b68170ba79dbbcc4`）。宿主日志报告加速度计监听器已注册。

该次 Guest `dumpsys sensorservice` 显示一个 `AEmulator 3-axis Accelerometer`，两个系统侧客户端已订阅，最近 50 条事件时间戳递增；手机静止时三轴读数约为 -0.14、0.16、9.84 m/s²。该观察证明这一启动中的系统侧枚举与事件传递，不证明普通第三方应用、设备运动与方向映射、准确度、暂停恢复、flush 或服务重启行为。未触碰或修改原始固件，未将原始日志加入 Git。

验证后向该 Guest 发送关机请求，日志记录停止和进程清理，检查时 Guest `system_server` 与 HIDL 传感器进程已退出，状态文件为非活动。停止时还记录了 `accelerometer state publication failed`，该日志的具体成因尚未单独排查；不能仅凭这条记录断定关机栅栏失效，本次读取到的状态确为非活动。运行副本配置和 HAL 的回滚方式仍见上文。

## 2026-11-12 普通应用晃动试用

在已有目标 Guest 内安装并打开临时普通应用 `app.redmagicemu.accelcheck`，它通过 Android `SensorManager` 前台订阅加速度计，显示 X/Y/Z 和事件数，不申请额外权限。实机画面显示“正在接收数据”和递增的事件数；用户晃动手机后反馈“这个是好的”。这补充了普通应用能接收以及用户观察到晃动响应的证据，但用户未提供逐轴方向或数值对照，不能据此确认坐标方向、精度或采样频率。暂停恢复、flush 和服务重启仍未验证。

临时 APK 为 `%TEMP%/rme-accel-display-ydc1wf5b/accel-check.apk`，SHA-256 `75d6f98cd22d180112e297ccec05f4105a0220070951f19b84e738765efc839c`；安装前检查同名 Guest 包不存在，安装后为用户测试保留该应用。两处传输暂存 APK 已按摘要核对并清理。需要移除时仅在 Guest 内卸载该包，不要卸载宿主标准版或删除实例。

## 2026-11-12 关机状态提示

用户报告关机时出现 `accelerometer state publication failed`。检查发现停止时主动将 `stateReady` 置为 false，随后注销监听器的流程把它误认为发布失败；实际发布异常仍由 `publishState` 单独记录。调整为仅在桥接服务运行时报告该状态。构建及相关单元测试通过；安装 standard Debug 后，实机以传感器开启启动客体，再从宿主菜单关闭，状态文件由活动变为非活动，未再出现该提示。该次系统音频索引完成；用户另报的关机期间索引异常及原厂 multihal 服务崩溃尚未修复。回滚本次提示修复可撤销 `Sensors.apply` 中对 `running` 的条件判断，不涉及原始固件或运行树文件。

## 2026-11-15 加速度计、陀螺仪与磁力计

- AEM2 保持原 32 字节布局，`type` 现在接受加速度计 1、磁场 2 和陀螺仪 4。HAL handle 依次为加速度计 1、陀螺仪 2、磁力计 3；三类分别维护 active、enabled_since、last_event、period、next_sample 和 flush fence。
- 宿主 `Sensors` 使用同一个 `SensorEventListener` 注册当前存在的三类默认传感器；某一类缺失不会阻止其余类型。每一类独立过滤会话前、重复/倒退、过旧和过新的样本，成功后写入同一 FIFO。桥接总开关、画面可见性和 `/dev/aemu_sensor_state` 会话栅栏仍同时约束三类。
- `n64_hal_test.c` 现覆盖三传感器枚举、类型/handle 映射、三轴数据、独立激活和周期、按 handle 完成 flush，以及原有 FIFO 损坏、暂停恢复、并发和关闭契约。WSL 编译与运行通过，Kotlin 协议定向测试通过。
- 使用 NDK 28.2.13676358 重建 system/vendor 两份目标 HAL，SHA-256 均为 `49b85c6411cd232ee44fb7ff42d76367734a27cd37698c0591a72f5528ab1ff9`。尚未安装或启动 Guest；应用枚举、真实陀螺仪/磁场数据、单位、方向、量程和长期稳定性仍未验收。

## 2026-11-15 指南针实机验收与 reporting mode 修复

- 原厂指南针首次实机启动后，ExitInfo 记录 `APP CRASH(EXCEPTION)`；DropBox 栈为 `requestLocationUpdates("gps")` 后抛出 `IllegalArgumentException: provider "gps" does not exist`。同期磁力计已能枚举，因此该闪退与传感器 HAL 无关。
- `LocationStub` 只在 NX809J/API 36/ARM64/N64 启动，通过 Guest 自身 `ILocationManager` 注册、启用 test provider `gps`，并在 system_server 更替后重新获取 Binder。宿主 `HostLocationBridge` 取得真实 network/GPS 位置后原子发布到 `/dev/aemu_location`；Guest 仅在文件含活动位置时调用 `setTestProviderLocation`，没有位置数据时不生成假坐标。
- 修复闪退后指南针仍固定不动。`dumpsys sensorservice` 显示 AEmulator 三类传感器被枚举为 `continuous | wakeUp`，而 AOSP Orientation 为 non-wake-up。根因是 HAL 把 `SENSOR_FLAG_CONTINUOUS_MODE` 定义为 `0x1`；该位实际是 `SENSOR_FLAG_WAKE_UP`，continuous reporting mode 应为 `0`。
- `n64_hal_test.c` 新增断言确认三类传感器 flags 为 `0` 且 wake-up 位未设置；WSL 原生契约测试通过。修复后目标 Guest 的枚举为 `non-wakeUp | continuous`，SensorService 连续显示加速度计事件，指南针能订阅 Orientation 并持续更新航向。
- 本轮重建后的 system/vendor 两份 HAL SHA-256 均为 `c35dc35e2d2a1efdb85fee11dc928a0e889d6d7089782bd1a72302672e147e83`；`aemu-stubs.jar` SHA-256 为 `8702b01cff582ead5011304e5d50959ebc992515b3052a3866422d7ae1b05426`。已在 `j2fxy4` 实例实际重启加载对应摘要。
- 实机结果：指南针持续显示航向，磁场约 `42 µT`，经纬度显示宿主最后已知网络位置；用户确认“完全可用”。经纬度依赖宿主定位授权和 network/GPS 最后位置，不代表 Guest 已完成独立 GNSS 卫星解算；宿主没有海拔时界面仍显示 `--`。
- 当前安装包为 `app/build/outputs/apk/standard/debug/app-standard-debug-location-signed.apk`，SHA-256 `52e594f185f34effb41bfbef69b0f234f0d004caf57d394839e0bea2f8694e91`。回滚必须同时撤销 HAL reporting-mode 修复、两份 HAL 资产、位置桥/GPS stub、定位权限与启动接线；只回退一端会造成航向不更新或指南针再次闪退。

## 2026-11-22 未校准陀螺仪和磁力计

- 宿主按 Android SensorManager 实际能力生成 Guest 运行树的 `/dev/.aemu-sensor-caps`。位 0 对应陀螺仪 `TYPE_GYROSCOPE_UNCALIBRATED`，位 1 对应磁场 `TYPE_MAGNETIC_FIELD_UNCALIBRATED`。Guest HAL 在初始化时读取一次，能力文件缺失或格式无效时仍枚举原三类传感器。
- 原 32 字节 AEM2 事件保留。新 48 字节 AEM3 事件包含宿主原始采样时间、三轴测量和三轴偏差，两组向量使用同一宿主自然轴变换。偏差和测量均拒绝非有限值。原有传感器开关、可见性、时间戳栅栏和 flush 顺序共同作用于新通道。
- 通过 `native/sensorshal/build_n64.py` 重建成对 N64 资产。要回退，可在关闭 Guest 后覆盖安装前一份同签名测试包，下一次启动由 APK 恢复运行树 HAL。源码回退需要同时撤销新桥接代码与两份 N64 资产，原厂固件无需改动。
