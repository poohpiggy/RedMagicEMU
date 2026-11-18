# 目标固件

RedMagicEMU 0.1.0 的任务 1B 使用固件声明的构建指纹进行准入。以下两个完整值任意一个匹配即可：

- 系统分区：`REDMAGIC/NX809J/NX809J:16/BQ2A.250705.001-BP2A.250605.031.A3/20260130.143527:user/release-keys`
- bootimage：`REDMAGIC/NX809J/NX809J:16/BQ2A.250705.001-BP2A.250605.031.A3/20260130.145957:user/release-keys`

支持读取 `ro.build.fingerprint`、`ro.system.build.fingerprint` 和 `ro.bootimage.build.fingerprint`。去除首尾空白后作区分大小写的完整比较；缺失、读取失败或全部不匹配时拒绝。导入保存实际匹配值；启动使用保存值，旧实例没有该字段时从现有系统属性补读。当前首版导入目标为 ZIP 镜像组合包，不新增 raw system.img 或 GSI 入口。

首批目标输入 `RedMagic_NX809J_VM.zip` 中，系统分区的指纹为 `143527`，派生 ramdisk 中的 bootimage 指纹为 `145957`；它们属于不同分区，历史日志指纹与系统分区值一致。该包的 ramdisk 为适配 AE 从 init_boot 派生。项目目标仍为 REDMAGIC NX809J Android 16/API 36/ARM64 Guest，不限制宿主型号。

2026-11-15 的新实例导入使用 `_VM_ztecfg` 固件，设备上的输入文件为 `RedMagic_NX809J_VM_ztecfg_codex_20261109_6e4c9f2a.zip`。该文件是当次导入输入；不据此声称它与上述 ZIP 字节相同，实际准入仍按导入指纹检查。

准入读取固件声明的构建指纹。正式固件以项目登记的目标样本为准。
