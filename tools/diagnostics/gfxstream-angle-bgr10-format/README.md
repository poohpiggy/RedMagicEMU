# gfxstream ANGLE BGR10 格式声明研究

这是电脑端独立研究，**未接入 EMU、未修改 Guest 或设备**。固定上游为 [google/gfxstream `cf47bd2660e7217f5729c91664d36d81c1b3282a`](https://github.com/google/gfxstream/tree/cf47bd2660e7217f5729c91664d36d81c1b3282a)，上游文件采用 Apache-2.0。本目录补丁修改该上游文件；测试脚本及测试源码采用 MIT，不包含完整上游源码或二进制。

固定源码在 `host/gl/color_buffer_gl.h` 中将 `GL_BGR10_A2_ANGLEX` 定义为 `0x6AF9`，原注释注明来源为 ANGLE。`frame_buffer.cpp` 的两个格式转换函数均使用它；GLES=0 时不包含原 GL 专用头，因此真实对象编译报常量未声明。候选补丁把**同一条定义及原注释**移到已有共享头 `gfxstream/host/gl_enums.h`，再让原 GL 头显式包含该共享头；没有改变数值、格式映射或删除格式支持。

## 红绿与转换函数复验

以下路径指向本次已有的 WSL 隔离源码和构建。Android 源码副本已应用此前两项独立的窗口类型和 color buffer 条件编译研究补丁，并带有仅供实验的 GLES 头脚手架及 Android 平台源文件选择；它们均未接入产品。脚本校验三个相关上游文件的固定 SHA-256，复制源码到新私有目录、严格应用本目录单项补丁，并使用给定构建的 `compile_commands.json`。不下载组件、不修改输入或访问设备。

```sh
G=/var/tmp/rme-gfxstream-build.hTnuwsWV
K=/var/tmp/rme-kumquat-research.r6Wg21lO
export LD_LIBRARY_PATH="$G/sysroot/usr/lib/x86_64-linux-gnu"
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-angle-bgr10-format/check.py \
  /var/tmp/rme-android-vk-offline.nVNDSuyG/source \
  /var/tmp/rme-android-vk-offline.nVNDSuyG/build \
  /var/tmp/rme-colorbuffer-sync-study.r8STw1aB/build-linux-gles \
  "$K/new-gfx-gles-build/host" "$G/sysroot/usr/lib/x86_64-linux-gnu"
```

测试检查 Android GLES=0 原版常量不可见、候选可见，且候选的真实 `frame_buffer.cpp` 不再报该常量错误；该对象仍因其他问题失败。Linux GLES=1 原版与候选的常量检查和真实对象编译均须通过。

脚本还从固定 `frame_buffer.cpp` **原样提取**两个 `GetGfxstreamFormat` 重载生成临时测试程序，不修改它们的逻辑：原版 GLES=0 因常量缺失编译失败，原版 GLES=1 与候选 GLES=0/1 均编译并运行，验证 `0x6AF9` 映射为 `B10G10R10A2_UNORM`、RGBA 正向对照和未知格式失败。该程序链接已构建的 GLES=1 后端以使用真实类型和日志，不初始化 GPU；它是转换函数切片测试，不等于 GLES=0 完整后端运行。脚本保留临时源码、命令、对象和日志，全部成功时输出 `ANGLE_FORMAT_CHECK_OK`。

实际 GLES=1 构建、运行时 Vulkan-only 的软件后端尝试创建此格式时，llvmpipe 报告不支持 `B10G10R10A2_UNORM`，创建失败并正常清理；原始日志为 `/var/tmp/rme-angle-format-study.JoLVFzjx/runtime-baseline.log`。因此本研究没有证明该格式真实图像读写可用。Android `frame_buffer.cpp` 的其他编译错误、完整后端链接、GL/Vulkan 双 backing 与设备运行均未验证。
