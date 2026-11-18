# gfxstream Vulkan 状态查询声明研究

本目录是电脑端独立研究，**未接入 EMU、未修改 Guest 或设备**。固定上游为 [google/gfxstream `cf47bd2660e7217f5729c91664d36d81c1b3282a`](https://github.com/google/gfxstream/tree/cf47bd2660e7217f5729c91664d36d81c1b3282a)，上游文件采用 Apache-2.0。本目录补丁修改该上游文件；测试脚本采用 MIT，不包含完整上游源码或二进制。

`FrameBuffer::Impl::hasEmulationVk()` 只检查 Vulkan 指针是否非空。原版将它放在 GLES 条件编译块内，而公共 `FrameBuffer::hasEmulationVk()` 无条件调用它，Android GLES=0 编译会报告该成员不存在。候选补丁将 getter **原样**移到条件块外，不改其函数体或其他方法。

## 红绿复验

以下路径指向本次已存在的 WSL 隔离研究构建。Android 源码副本已应用此前两项独立的窗口类型、color buffer 条件编译研究补丁，还包含仅用于编译探索的 `gles_compat.h` 标准头脚手架及 Android 平台源文件选择；这些均不是产品实现。脚本校验 `frame_buffer.cpp` 的固定 SHA-256，在新临时目录复制源码、严格应用本目录单项补丁，使用两个现有构建的 `compile_commands.json` 执行真实对象编译，不下载组件或修改输入。需有 Python 3、`patch`、`objcopy`、`objdump` 和对应编译工具链。

```sh
G=/var/tmp/rme-gfxstream-build.hTnuwsWV
export LD_LIBRARY_PATH="$G/sysroot/usr/lib/x86_64-linux-gnu"
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-framebuffer-vkdecl/check.py \
  /var/tmp/rme-android-vk-offline.nVNDSuyG/source \
  /var/tmp/rme-android-vk-offline.nVNDSuyG/build \
  /var/tmp/rme-colorbuffer-sync-study.r8STw1aB/build-linux-gles
```

测试要求 Android GLES=0 的原版日志含 `no member named 'hasEmulationVk'`，候选日志不含该错误；**原版和候选的 Android 对象均仍因其他错误编译失败**。Linux GLES=1 的原版和候选对象均须编译成功，并比较生成的 getter 机器码和重定位记录。本次比较一致；这不是对所有平台、编译器或运行时行为的证明。

成功输出 `GETTER_DIAGNOSTIC_OK`，并打印保留命令、完整日志、对象和比较结果的临时目录。该结果只说明这条错误已消除，不代表 Android 对象、完整后端、图形输出或手机 Vulkan 可以使用。其余 GLES 类型、读回、上下文及其他条件编译错误仍待逐项分析，本测试未运行设备。
