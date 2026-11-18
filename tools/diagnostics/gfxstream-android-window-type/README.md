# gfxstream Android 窗口类型研究

这是独立的电脑端编译实验，**未接入 EMU、未修改 Guest 或设备**。目标源码固定为上游 [google/gfxstream `cf47bd2660e7217f5729c91664d36d81c1b3282a`](https://github.com/google/gfxstream/tree/cf47bd2660e7217f5729c91664d36d81c1b3282a)，上游文件采用 Apache-2.0 许可证。本目录的测试代码和脚本采用 MIT；补丁是对该 Apache-2.0 上游文件的单行修改，不包含完整上游源码或二进制。

上游 Android 实现使用 `ANativeWindow*`。原头文件在关闭 GLES 时将 `EGLNativeWindowType` 声明为 `void*`，Android 交叉编译 `native_sub_window_android.cpp` 时无法将其传给 `ANativeWindow_release`。补丁让 Android 使用 NDK EGL 头中真实的类型，其他平台维持原有条件。

脚本验证指定源码中的三个相关文件的固定 SHA-256，将它们复制到新建的私有临时目录，再执行以下检查：

1. Android ARM64/API 29，GLES=0：原版类型断言及真实窗口源码编译均应以指定错误失败；GLES=1 均应通过。
2. Linux，GLES=0：确认原版的 `EGLNativeWindowType` 是 `void*`。
3. 只在临时副本应用本目录补丁；重复 Android GLES=0/1 类型检查和对象编译，以及 Linux 对照，均应通过。

脚本对每次运行建立新目录并输出其路径，保留编译日志和对象文件，不下载组件或修改输入源码。它检查预期失败的退出码及诊断，缺少编译器、源码哈希不匹配或其他编译错误不会被当作预期失败。

## 复验

在 Linux/WSL 传入固定源码目录、Android NDK r28c 根目录、Linux C++ 编译器绝对路径以及该编译器可选参数：

```sh
bash /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-android-window-type/run.sh \
  /var/tmp/rme-kumquat-research.r6Wg21lO/google-gfxstream-cf47 \
  /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c \
  /usr/bin/c++
```

上述路径是本次实验环境的示例，需要替换为本机已有路径。WSL 环境中如果系统没有 `/usr/bin/c++`，本次还用以下隔离 GCC 15 工具链复验：

```sh
C=/var/tmp/rme-clvk-build-dEVbhfMR
export LD_LIBRARY_PATH="$C/sysroot/usr/lib/x86_64-linux-gnu"
export COMPILER_PATH="$C/sysroot/usr/libexec/gcc/x86_64-linux-gnu/15"
export CPLUS_INCLUDE_PATH="$C/sysroot/usr/include/c++/15:$C/sysroot/usr/include/x86_64-linux-gnu/c++/15:/usr/lib/gcc/x86_64-linux-gnu/15/include"
bash /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-android-window-type/run.sh \
  /var/tmp/rme-kumquat-research.r6Wg21lO/google-gfxstream-cf47 \
  /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c \
  "$C/sysroot/usr/bin/x86_64-linux-gnu-g++-15" -B/usr/lib/gcc/x86_64-linux-gnu/15/
```

Android 编译会清除宿主 C/C++ 头文件与工具链搜索环境变量，以免 Linux 工具链污染 NDK 交叉编译。成功时末尾输出 `ANDROID_WINDOW_TYPE_TEST_OK`，同时保留原版预期失败日志。

本实验只证明类型一致和单个真实源文件可编译，**不证明完整 gfxstream 后端可以链接、Android 窗口有效或图形/Vulkan 可以运行**。该版本的 Vulkan-only GLES 引用，以及 Android 被误判为 X11 的其他路径，仍需分别分析和测试；本补丁不处理这些问题，也不改变窗口有效性检查。
