# Vulkan 分配与 AHardwareBuffer 导出探针

这是独立诊断，**未接入 EMU，不修改 Guest 或原厂镜像**。它在单个进程内通过 Android NDK 和 Vulkan 接口分配一个可导出的 buffer，获取 AHardwareBuffer，并核对其描述；它不使用 socket、跨进程句柄传递或网络功能。

探针对 256 字节 buffer 使用 gfxstream 路线中的六种用途：index、vertex、storage、storage texel、transfer source 和 transfer destination。它检查 Vulkan 1.1、AHB 和 foreign queue 扩展及外部 buffer 能力，再创建 Vulkan buffer、申请并绑定可导出内存，通过 `vkGetMemoryAndroidHardwareBufferANDROID` 获取 AHB。导出成功及 BLOB 描述一致时输出 `AHB_EXPORT_OK`；这一步不执行计算或数据读写。

只有导出的 AHB 同时报告有效的 CPU 读、写用途时，探针才继续 CPU 写入、Vulkan 复制与填充、CPU 读回，并逐项比较 64 个整数。正常完整成功需要 `AHB_CPU_TO_VK_OK`、`AHB_VK_TO_CPU_OK`、`AHB_VULKAN_EXPORT_ROUNDTRIP_OK` 和退出码 0。`--expect-mismatch` 用于故意改变完整读回阶段的一个预期结果；若设备提前报告不支持，不会执行这个负对照。

程序对 fence 等待设置 10 秒上限；其他驱动或 AHB 调用仍可能阻塞，设备运行需另外设置进程级超时。队列完成状态或 AHB unlock 结果未知时，不提前销毁可能在用的对象，而由进程结束处理；这不保证所有驱动故障都能安全恢复。

## 电脑端验证和 Android 交叉构建

在 Linux/WSL 指定已有的 Android NDK、Vulkan 头文件和 Android 测试头文件；脚本不下载依赖，也不连接设备。它在 `/var/tmp` 新建目录并保留日志：

```sh
bash /mnt/e/RedMagicEMU/tools/diagnostics/vulkan-ahb-export/run.sh \
  /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c \
  /var/tmp/rme-kumquat-research.r6Wg21lO/google-gfxstream-cf47/third_party/vulkan/include \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/src/guest/mesa/include/android_stub
```

脚本运行数据校验及 CPU usage 解析测试，再以真实 llvmpipe 查询与**只能返回失败**的 Linux AHB 测试替身验证不支持路径。它要求输出相应 AHB buffer 查询和明确的不支持原因；不把替身当作 Android 运行或模拟成功。该负对照依赖指定 ICD 的结果，可通过 `RME_VULKAN_ICD` 选择已有 ICD；若驱动结果不同，脚本失败而不伪报通过。

脚本最后交叉编译 Android ARM64/API 29，校验 ELF 架构、动态链接器、Vulkan/Android 依赖和 16 KiB LOAD 对齐。本次构建输出 SHA-256 为 `68e675310ee86e1851b3c42dbec246aa870ca5d7af7aac2664fe41d53d3814d5`；二进制不纳入 Git，不同构建环境可能产生不同摘要。源文件 `probe.c` SHA-256 为 `84f04aa6d37d58546f6f6b17c7239038e2e16e06bec617f385795f5c623627e9`。工具和依赖路径均是本机临时实验环境，不是完整构建包。

## 手机上已验证的范围

2026-11-17 在用户授权的手机宿主上，Adreno 750 对该六用途 buffer 成功导出 256 字节 BLOB，输出 `AHB_EXPORT_OK`，描述的 usage 为 `0x10000ff`。该值的 CPU 读写字段不是探针支持的 `RARELY` 或 `OFTEN` 模式，因此探针输出 `UNSUPPORTED exported AHB BLOB lacks requested CPU read/write usage` 并以 1 退出，没有尝试 CPU lock 或双向往返。临时程序执行后已删除。

这只证明该次 Vulkan 分配和 AHB 导出成功；**CPU 往返没有执行**，也不证明 GPU 计算、其他用途、不同资源参数、跨进程共享、EMU、Guest 或 Geekbench 可用。先前另一个先分配 AHB 再导入 Vulkan 的测试采用不同分配方向和用途，见 [`vulkan-ahb-roundtrip`](../vulkan-ahb-roundtrip/README.md)，不能将两个结果互相替代。
