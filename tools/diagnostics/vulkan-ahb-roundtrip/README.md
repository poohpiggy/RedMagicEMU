# Android AHardwareBuffer 与 Vulkan 单进程读写探针

本目录是独立诊断，**不接入 EMU，也不修改 Guest 或原厂镜像**。探针在同一进程中使用 Android NDK 的 AHardwareBuffer 和 Vulkan 公开接口，检查一个 BLOB 缓冲区的双向数据访问。它不使用 socket、跨进程句柄传递或任何网络功能。

## 测试内容

1. 查询 Vulkan 设备，要求 Vulkan 1.1、`VK_ANDROID_external_memory_android_hardware_buffer`、`VK_EXT_queue_family_foreign`、`samplerYcbcrConversion` 以及可用的 transfer 队列。本探针使用的资源是 256 字节 BLOB，usage 为 AHardwareBuffer 的 GPU 数据、CPU 读写以及 Vulkan 的 transfer source/destination。
2. 针对上述实际 buffer 用途查询 AHB 外部内存是否可导入。申请硬件缓冲区并检查返回的格式和用途；CPU 写入 64 个整数并以阻塞的 `AHardwareBuffer_unlock(..., NULL)` 完成释放。
3. 创建 Vulkan buffer，按 `vkGetAndroidHardwareBufferPropertiesANDROID` 返回的大小和内存类型，与 Vulkan buffer 要求取交集后导入；使用 `VK_QUEUE_FAMILY_FOREIGN_EXT` 将所有权转给 Vulkan。Vulkan 将数据复制到普通 staging buffer，再填充原 AHB buffer；两个操作之间有传输屏障，随后将 AHB 所有权释放回 FOREIGN，并等待 Vulkan fence。如果该具体配置不支持，或任一步调用失败，输出原因并非零退出。
4. fence 成功后，将 staging 数据逐项与 CPU 输入比较；按需 invalidate 非 coherent 内存。再由 CPU 锁定 AHB 并逐项核对 Vulkan 写入的值，最后释放资源。`--expect-mismatch` 故意改变一个预期值，用来验证检查确实会失败；不能把该选项的失败当成驱动故障。

正常成功应同时出现 `AHB_CPU_TO_VK_OK words=64`、`AHB_VK_TO_CPU_OK words=64`、`AHB_VULKAN_ROUNDTRIP_OK`，且进程退出码为 0。错误预期应报告 `intentional expectation mismatch` 并以 1 退出。设备名和类型由驱动报告；本探针只执行 Vulkan 的 copy/fill 传输，不运行计算 shader。

Vulkan fence 等待设置为 10 秒；AHB lock/unlock 和其他驱动调用仍可能阻塞，因此设备运行还必须另设进程级超时。如果队列完成状态或 unlock 结果未知，程序不会提前销毁可能仍在使用的资源，留给进程退出处理；这不能证明设备驱动在异常情况下的所有清理行为。

## 电脑端构建和负向对照

在 Linux/WSL 使用已有的 NDK、Vulkan 头文件、Android 测试头文件和本机 Vulkan ICD。脚本不下载依赖，不连接设备；它在 `/var/tmp` 新建输出目录并保留日志：

```sh
bash /mnt/e/RedMagicEMU/tools/diagnostics/vulkan-ahb-roundtrip/run.sh \
  /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c \
  /var/tmp/rme-kumquat-research.r6Wg21lO/google-gfxstream-cf47/third_party/vulkan/include \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/src/guest/mesa/include/android_stub
```

上述路径是本机临时构建位置，不是随仓库提供的依赖。脚本会运行纯数据校验测试，随后以真实 llvmpipe Vulkan 查询和仅能返回失败的 Linux AHB 测试替身检查不支持路径；替身不会模拟成功的 Android 硬件缓冲。脚本要求实际出现 AHB buffer 查询结果，并在该查询拒绝导入或替身拒绝配置时返回预期失败。它还使用 NDK 交叉编译 Android ARM64/API 29，检查 ELF 架构、动态链接器、Vulkan/Android 依赖与 16 KiB LOAD 对齐；此步骤本身不在 Android 执行。`RME_VULKAN_ICD` 可指定已有的本机 ICD，其他驱动对该配置可能给出不同结果。

本机电脑复验中，llvmpipe 对该 AHB transfer buffer 报告不可导入，探针在调用测试替身前按预期非零退出；这并不说明手机不支持。使用 NDK r28c 的交叉编译结果 SHA-256 为 `af298263227563291555da91445b875a0ebb1d2d7cbcc1391d32d282aee5406a`；`probe.c` 的 SHA-256 为 `7d56502940ef453bd1a40219c015b3c82201d233e4537f095ece74622f326126`，`verify.h` 为 `1cef5fd58770c5329d6d267270470b2c67547aea354d5183949fe9dc9cfd584b`。二进制未纳入 Git，不同构建环境可能产生不同摘要。

## 手机上已验证的范围

2026-11-17，在用户授权的手机宿主上，Adreno 750 对此单进程 BLOB 用例完成 64 项 CPU→Vulkan 和 Vulkan→CPU 校验；故意错误预期的对照返回失败。该结果不代表 EMU 或 Guest 已支持 Vulkan/OpenCL，也不证明 AHB 图像、Vulkan shader、跨进程共享、其他资源参数、应用链接器命名空间或 Geekbench GPU 可用。探针没有操作 Guest，也没有直接访问或修改硬件设备节点。
