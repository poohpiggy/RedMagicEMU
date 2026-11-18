# gfxstream Vulkan-only color buffer 编译研究

这是电脑端独立研究，**没有接入 EMU，也没有修改 Guest 或设备**。固定上游为 [google/gfxstream `cf47bd2660e7217f5729c91664d36d81c1b3282a`](https://github.com/google/gfxstream/tree/cf47bd2660e7217f5729c91664d36d81c1b3282a)，相关上游源码采用 Apache-2.0。本目录补丁修改该上游文件，测试代码和脚本采用 MIT，不包含完整上游源码或二进制。

GLES=0 时 `mColorBufferGl` 未声明，但五个跨后端同步方法仍直接检查它，导致 Linux 和 Android 对象编译失败。候选补丁只把五处检查改为已有的 `getColorBufferGl()`：GLES=1 时该 getter 返回同一 `unique_ptr` 的原始指针，GLES=0 时返回空指针；同步逻辑与返回语句保持原样。

原逻辑在任一 backing 缺失时将这些**跨后端同步操作**视为无需同步并返回 true，补丁保持这一行为；不能将该返回值当成已经创建 GL backing 或上传了数据。特别是无 GL backing 时 `flushFromVkBytes` 不会把传入字节写入 Vulkan。上游 `updateGlFromBytes` 在缺少 GL backing 时返回成功是另一个问题，本补丁不修复它，另见 [`gfxstream-color-buffer`](../gfxstream-color-buffer/README.md)。

## 对象编译红绿测试

脚本接受已存在的固定上游源码、Linux GLES=1 和 Android GLES=0 Meson 构建目录，读取后两者的 `compile_commands.json`，创建私有临时源码副本并记录实际命令和日志；不会修改输入、下载组件或运行设备。上游原树缺少 GLES=0 时引用的 `gles_compat.h`，本目录提供仅含标准 EGL/GLES 头引用的**对象编译脚手架**，它不是完整后端实现。

```sh
G=/var/tmp/rme-gfxstream-build.hTnuwsWV
K=/var/tmp/rme-kumquat-research.r6Wg21lO
A=/var/tmp/rme-gfxstream-cf47-android.lVju0gMk
export LD_LIBRARY_PATH="$G/sysroot/usr/lib/x86_64-linux-gnu"
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-color-buffer-nogles/compile-check.py \
  "$K/google-gfxstream-cf47" "$K/new-gfx-gles-build" "$A/build-vulkan"
```

原版 Linux/Android GLES=0 均应因 `mColorBufferGl` 失败，候选均应通过；Linux GLES=1 的原版和候选也应通过。成功输出 `COLOR_BUFFER_OBJECTS_OK`。这些临时路径和构建依赖须事先存在；脚本校验目标上游源文件 SHA-256，但不重建依赖。

## 软件 Vulkan 行为对照

下面使用两个**已构建的 GLES=1 后端**：原版和应用候选补丁的版本。探针启用运行时 `GuestVulkanOnly`、`HostAllocation` 与 `SystemBlob`，使用指定的 llvmpipe ICD；它创建真实 Vulkan color buffer，验证 64 字节写入和读回、三种 Vulkan 同步调用后图像不变、未知 handle 返回失败，并检查资源关闭和 `FrameBuffer::finalize()` 正常返回。两个后端各在新进程中运行并使用独立缓存目录；超时将有界清理本次自建进程组，不将信号退出当作通过。

```sh
R=/var/tmp/rme-colorbuffer-sync-study.r8STw1aB
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-color-buffer-nogles/runtime-check.py \
  "$K/new-gfx-gles-build" "$K/new-gfx-gles-build/host" \
  "$R/build-linux-gles/host" "$G/sysroot/usr/lib/x86_64-linux-gnu" \
  /usr/share/vulkan/icd.d/lvp_icd.json
```

本次两组均通过，日志报告的设备是 llvmpipe CPU 软件设备。脚本记录传入库的哈希；调用者须核对候选库确实由本补丁构建。候选 GLES=1 后端的隔离构建命令与日志位于 `$R/build-linux-gles.sh` 和 `$R/build-linux-gles.log`，原版及候选运行日志位于脚本输出的临时目录。之前未指定 HostAllocation 的独立尝试在 Vulkan 初始化时因 color buffer 外部内存分配失败而 abort，原始日志保留在 `$R/framebuffer-run.log`。

Linux GLES=0 的完整后端在本次实验中仍因 `frame_buffer.cpp` 的 EGL 类型、GL 常量和无保护 GLES 类型引用等其他问题无法编译，因此**没有 GLES=0 后端运行结果**。本研究也没有 Android 运行结果，没有测试 GL 与 Vulkan 同时存在、共享或复制的双 backing 路径；不能据此声称完整图形后端或手机 GPU 可用。
