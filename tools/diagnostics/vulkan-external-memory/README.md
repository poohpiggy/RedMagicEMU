# Vulkan 外部内存能力探针

这是电脑侧诊断工具，**不是 EMU 图形实现**。它对 Vulkan 1.1 及以上的每个物理设备查询外部内存属性，不创建设备、不分配 Vulkan 内存，也不导入或导出任何句柄。不会修改 EMU、Guest 或设备。

查询使用以下固定参数，不能将结果推广到其他 buffer usage、格式或 tiling：

- Buffer：`flags=0`，`usage=STORAGE_BUFFER`。
- Image：`R8G8B8A8_UNORM`、2D、optimal tiling、`usage=SAMPLED|COLOR_ATTACHMENT`、`flags=0`。
- Handle：`OPAQUE_FD`、`DMA_BUF`、`HOST_ALLOCATION`、Android `AHARDWAREBUFFER`。只有设备通告相应扩展时才查询该 handle；否则报告 `not_queried`。Android 专有类型也遵循此规则。

每种资源和 handle 分别输出驱动返回的原始 feature、compatible 和 export-from-imported 位掩码，以及该 handle 的 `IMPORT`、`EXPORT`、`BOTH` 和 `DEDICATED_ONLY` 判断。`BOTH=1` 只表示这一个资源参数和 handle 的查询同时报告可导入及可导出，**不表示两个设备、跨进程传输或 EMU 已能互通**。`DEDICATED_ONLY` 是分配约束，不表示独立 GPU。Image 查询若返回 `VK_ERROR_FORMAT_NOT_SUPPORTED`，会明确输出该结果，而不是当作成功。查询 host allocation 扩展时还输出报告的最小 host 指针对齐；实际导入还需要为具体指针查询内存类型并满足其他要求。

探针还分别对 buffer 和 image 判断两个多句柄导出组合：`0x201`（opaque FD | DMA-BUF）与 `0x280`（DMA-BUF | host allocation）。只有所有成员均已查询、各自报告可导出且各自的 `compatibleHandleTypes` 都覆盖整个组合，才报告 `supported`；查询齐全但不满足条件或格式明确不支持时报告 `unsupported`。扩展未通告、查询缺失或发生无法判断的错误时报告 `unknown`，即使另一成员已经表现出限制也不把不完整查询当成完整结论。这些组合结果只代表驱动对上述固定创建参数的报告，并不执行分配。

驱动返回的能力是运行前提，不保证分配、同步、数据正确性或应用兼容性；`OPAQUE_FD` 也不能因此视为可直接映射的共享内存。[Vulkan external buffer 查询](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceExternalBufferProperties.html)、[external image 查询](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceExternalImageFormatInfo.html)和 [host allocation 扩展](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_external_memory_host.html)说明了这些查询的范围。

## 电脑验证

WSL 需要 C 编译器、Vulkan loader、llvmpipe ICD 和 Android NDK r28c。命令不下载安装组件：

```bash
bash /mnt/e/RedMagicEMU/tools/diagnostics/vulkan-external-memory/run.sh \
  /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
```

脚本运行能力判断单元测试、强制使用 llvmpipe 的真实查询、缺失 ICD 的失败对照，并交叉编译 Android ARM64/API 29，检查 ELF 架构、Android 动态链接器、Vulkan 依赖和 16 KiB 加载段对齐。日志和交叉编译产物保存在脚本输出的 `/var/tmp/rme-vulkan-external-memory.*` 中。设置 `RME_VULKAN_ICD` 可明确选择另一个本机 ICD；设备名称和类型均为驱动报告，不据此证明真实硬件加速。

本次 llvmpipe 查询中，目标 buffer 和 image 的 opaque FD 均报告可导入及导出；DMA-BUF 和 host allocation 均报告可导入、不可导出。对这两类资源，`0x201` 和 `0x280` 两个组合均报告不支持。驱动虽通告 Android AHB 扩展，但目标 buffer 未报告导入或导出，目标 image 报告格式不支持。这些仅是该软件驱动当时的查询结果。Android 产物**仅完成交叉编译，尚未在手机运行**。
