# gfxstream Vulkan 初始化缺失函数的研究

本目录是电脑端研究，**未接入 RedMagicEMU**。补丁针对 [google/gfxstream 固定提交
`cf47bd2660e7217f5729c91664d36d81c1b3282a`](https://github.com/google/gfxstream/tree/cf47bd2660e7217f5729c91664d36d81c1b3282a)。
补丁涉及的上游文件版权属于 Android Open Source Project，采用 Apache-2.0 许可证；应用补丁时须保留原文件中的版权和许可证。这里的测试源码和脚本采用 MIT 许可证，不包含上游源码或二进制。

上游根据宿主报告认定支持 Vulkan 设备属性查询，却在两个函数名均无法解析时只输出警告，随后调用空指针。测试 shim 仅在指定测试服务进程中模拟两种名称不可用，其余 Vulkan 调用转发给真实 loader。在 llvmpipe 上，原版服务端复现 SIGSEGV；研究补丁在缺失时拒绝 Vulkan 初始化并释放实例。初始化失败后，上游另一处原本会无条件等待 FrameBuffer 就绪，因此补丁同时让空 renderer 直接返回错误；服务端最终收到 `ComponentError(-22)` 并以状态 1 退出。

回归包括：原版双名缺失、修正版正常计算、修正版仅缺 KHR 别名以及修正版双名缺失。双名缺失时检查两种名称的查询、错误传播和同一个 Vulkan 实例的创建与销毁；正常和仅缺 KHR 时通过独立客户端校验 64 个整数。服务端正常测试由 SIGTERM 结束，退出 143 是强制结束，不是优雅关闭。测试使用的 `SystemBlob` 在非 Windows 平台仍受上游限制。

**仅缺核心名称时仍有独立问题**：宿主初始化可以使用 KHR 别名，但随后 Guest 新建实例未必启用该扩展，decoder 查询失败；实验中客户端超时、服务端随后崩溃。此目录未修复或宣称通过该场景。驱动不符合自身能力报告时的其余错误、其他初始化失败路径、设备资源及同步生命周期也未全面审计。

## 复验已有实验构建

以下命令只使用已准备的本机文件，不下载或安装依赖。测试需要 Python 3、C 编译器、`unshare`、私有用户/挂载/PID/网络命名空间及可用的 llvmpipe Vulkan ICD；测试提供的路径及二进制由调用者信任，不证明其来源。它为每次运行创建新的 `/var/tmp/rme-missing-properties2-*` 目录和私有 socket，并使用相邻诊断的进程组监督器设置上限。日志可能包含本机路径和程序输出。没有 Android、真实 GPU 或 Geekbench 验证。

```sh
REPO=/mnt/e/RedMagicEMU
K=/var/tmp/rme-kumquat-research.r6Wg21lO
G=/var/tmp/rme-gfxstream-build.hTnuwsWV
R=/var/tmp/rme-vk-null-query.l8P7R6aJ

python3 "$REPO/tools/diagnostics/gfxstream-missing-properties2/verify.py" \
  --baseline-host "$K/new-gfx-gles-build/host" \
  --patched-host "$R/build/host" \
  --headers "$K/google-gfxstream-cf47/third_party/vulkan/include" \
  --server "$K/target-9f22/debug/kumquat" \
  --probe "$K/probe-system-icd" \
  --client-icd "$K/mesa-build-pinned2/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.x86_64.json" \
  --host-icd /usr/share/vulkan/icd.d/lvp_icd.json \
  --system-loader /lib/x86_64-linux-gnu/libvulkan.so.1 \
  --dependency-dir "$G/sysroot/usr/lib/x86_64-linux-gnu"
```

上述临时构建使用 Mesa `6e4a77089e1db6cb6269ec694de3a1a131f96689`、Kumquat/Rutabaga `9f22cc994019486a03086992609c0aa4769ae4b0`；Cargo.lock SHA-256 为 `7a786791e5c1eaea93204b3941d6d00635888fc8d35d03362a862e6dfe6a596d`。脚本记录主要输入摘要，但不覆盖所有传递依赖，也不能单独重建这些临时构建。

## 在新的隔离目录重建补丁版

以下是本机实验所用的 Meson 配置，要求上述固定源码、隔离编译器和依赖已存在；不更改源目录。对新建临时目录执行：

```sh
REPO=/mnt/e/RedMagicEMU
K=/var/tmp/rme-kumquat-research.r6Wg21lO
G=/var/tmp/rme-gfxstream-build.hTnuwsWV
R=$(mktemp -d /var/tmp/rme-vk-properties2-build.XXXXXXXX)
cp -a "$K/google-gfxstream-cf47" "$R/src"
patch -d "$R/src" -p1 < "$REPO/tools/diagnostics/gfxstream-missing-properties2/missing-properties2-failure.patch"
export PATH="$G/sysroot/usr/bin:/usr/bin:/bin"
export PYTHONPATH="$G/sysroot/usr/lib/python3/dist-packages"
export LD_LIBRARY_PATH="$G/sysroot/usr/lib/x86_64-linux-gnu"
export CXX="$G/bin/cxx"
python3 "$G/sysroot/usr/bin/meson" setup "$R/build" "$R/src" \
  --wrap-mode=nodownload -Dgfxstream-build=host -Ddecoders=gles,vulkan,composer \
  -Dplatforms= -Dcpp_args="-I$G/sysroot/usr/include" \
  -Dpkg_config_path="$G/prefix/lib/pkgconfig"
ninja -C "$R/build" -j 4
```

构建后在复验命令中将 `--patched-host` 指向 `$R/build/host`。编译、上述软件测试、EMU 接入与 Android 运行是不同阶段；这里仅完成前两者。
