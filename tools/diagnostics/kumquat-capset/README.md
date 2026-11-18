# Kumquat capset 拒绝实验

这是独立的电脑端研究，**尚未接入 EMU，也没有安装到设备**。研究对象为上游 [rutabaga_gfx 固定提交 9f22cc994019486a03086992609c0aa4769ae4b0](https://github.com/magma-gpu/rutabaga_gfx/tree/9f22cc994019486a03086992609c0aa4769ae4b0)，许可证为 [BSD-3-Clause](https://github.com/magma-gpu/rutabaga_gfx/blob/9f22cc994019486a03086992609c0aa4769ae4b0/LICENSE)。本目录只保存最小研究补丁和自行编写的测试，不包含上游完整源码或二进制。

固定上游在查询或创建 context 时，会将未启用 capset 回退到默认组件。配合 gfxstream 后端，服务器只枚举 Vulkan 时仍可能为未枚举的 GLES 或未知 ID 返回成功。研究补丁在 Kumquat 服务端按启用的 mask 检查查询 ID 和 context ID；遇到未启用 ID 时仅注销对应连接，其他错误按原路径传播。当前协议没有通用错误响应，因此拒绝表现为连接关闭，**不是空的成功响应**。

## EOF 与 capset 的合并研究补丁

[`combined-eof-capset.patch`](combined-eof-capset.patch) 针对上面同一固定原版，合并了 EOF 连接回收和 capset 单连接拒绝。它已经分别通过 [EOF 真实 socket 回归](../kumquat-eof/README.md#当前电脑上的原版和修正版回归)和下方 capset 真实 socket 回归，两个测试均包含正常 Vulkan 计算；仅为电脑端研究，**未接入产品**。

合并补丁应单独应用到固定原版，**不可与任一单项补丁重复应用**。它仍未解决 context、resource、fence 的归属和断线回收；拒绝旧式默认 capset 回退的兼容性也尚未解决。

## 已知限制

- 本补丁**没有修复普通 EOF FD 泄漏，也没有解决 context、resource、fence 所有权或断线回收**。已有真实实验显示，断线后的 context 仍存在，其他连接可操作；resource 仅凭可猜测 ID 可被其他连接 attach。它不能作为完整客户端隔离或产品可用的证明。
- 严格检查会拒绝未在 mask 中启用的 context ID，包括依赖旧式默认回退的客户端；兼容性尚未评估。无认证的跨客户端资源共享及其引用计数也仍需单独设计。
- 验证使用 llvmpipe CPU 后端及 `VulkanExternalMemoryMode:HostAllocation,SystemBlob:enabled`。上游警告 SystemBlob 在非 Windows 上尚未充分测试；不能推断 Android 或 Adreno 行为。上游 suspend/resume 仍有 TODO，本实验不测试状态保存。
- 测试在新用户和挂载命名空间绑定私有 `/tmp`，日志留在新建的 `/var/tmp/rme-kumquat-capset-*`。测试结束后以 SIGTERM 停止服务端；`server_exit_after_test=-15` 表示信号终止，不能称作优雅退出。
- `submit-gate.c` 仅暂停一个已有客户端真正的 `vkQueueSubmit`，然后调用实际 Vulkan loader，以确认拒绝其他连接后该客户端仍可完成计算。它不更改驱动或生产程序。

## 当前电脑上的原版和修正版回归

以下命令依赖本次保存在 `/var/tmp` 的固定组件：gfxstream `cf47bd2660e7217f5729c91664d36d81c1b3282a`、Mesa `6e4a77089e1db6cb6269ec694de3a1a131f96689`、对应构建产物及 `tools/diagnostics/vulkan-compute` 探针。临时目录失效后必须先重建这些组件；本目录不提供未知来源的预编译文件。

在 WSL 同一 shell 中运行：

```sh
k=/var/tmp/rme-kumquat-research.r6Wg21lO
g=/var/tmp/rme-gfxstream-build.hTnuwsWV
p=/var/tmp/rme-kumquat-capset-task.nhIGqgbP
t=/mnt/e/RedMagicEMU/tools/diagnostics/kumquat-capset

python3 "$t/run.py" \
  --server "$k/target-9f22/debug/kumquat" \
  --probe "$k/probe-system-icd" \
  --client-icd "$k/mesa-build-pinned2/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.x86_64.json" \
  --host-icd /usr/share/vulkan/icd.d/lvp_icd.json \
  --vulkan-include "$k/google-gfxstream-cf47/third_party/vulkan/include" \
  --library-path "$k/new-gfx-gles-build/host:$g/sysroot/usr/lib/x86_64-linux-gnu"
echo "original_exit=$?"

python3 "$t/run.py" \
  --server "$p/target/debug/kumquat" \
  --probe "$k/probe-system-icd" \
  --client-icd "$k/mesa-build-pinned2/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.x86_64.json" \
  --host-icd /usr/share/vulkan/icd.d/lvp_icd.json \
  --vulkan-include "$k/google-gfxstream-cf47/third_party/vulkan/include" \
  --library-path "$k/new-gfx-gles-build/host:$g/sysroot/usr/lib/x86_64-linux-gnu"
echo "fixed_exit=$?"
```

测试先确认合法 Vulkan capset 查询和 context 创建、由所有者销毁均成功；随后分别查询未启用的 GLES ID 8、未知 ID 255、越界 ID 0xffffffff，并尝试以 ID 8 和 255 创建 context。修正版必须只关闭各自的请求连接，保持已有控制连接和 Vulkan 客户端可用；之前已建立设备的客户端和新客户端还必须分别完成 64 项逐项验证。

本机原版退出 1，三个非法查询均得到成功响应，两个非法 context 均创建成功；修正版退出 0，五个非法请求各自导致连接关闭，合法查询和两个真实 Vulkan 计算均通过。完整基线记录 `/var/tmp/rme-kumquat-capset-k07bwbrc`，独立修正版记录 `/var/tmp/rme-kumquat-capset-zi6a55_6`。测试的 `server_alive_after_test=True` 表示服务在测试结束前存活。

## 测试超时清理

测试使用新的独立进程组，并检查服务器及客户端均继承该组。超时后只向该组发送 SIGTERM、等待 0.25 秒，再发送 SIGKILL；在信号发完前不回收启动进程，避免 PID 被复用成无关进程组。以下测试仅创建自身的进程树和另一个进程组内的哨兵：

```sh
python3 "$t/timeout-cleanup-test.py" --baseline  # 预期退出 1：旧处理留下后代
python3 "$t/timeout-cleanup-test.py"             # 预期退出 0：后代终止，哨兵存活
```

## 在新目录重建修正版

本机 Rust 为 1.93.1。使用本次实验的固定源码和 Cargo 缓存；`Cargo.lock` 是实验时解析并保存的锁文件，SHA-256 为 `7a786791e5c1eaea93204b3941d6d00635888fc8d35d03362a862e6dfe6a596d`。以下命令不会安装或覆盖原始源码：

```sh
build=$(mktemp -d /var/tmp/rme-kumquat-capset-build.XXXXXX)
cp -a "$k/rutabaga-9f22" "$build/src"
(cd "$build/src" && patch -p1 < "$t/capset-isolation.patch")
export PATH="$g/sysroot/usr/bin:$PATH"
export LD_LIBRARY_PATH="$g/sysroot/usr/lib/x86_64-linux-gnu"
export CARGO_HOME="$k/cargo-home"
export CARGO_TARGET_DIR="$build/target"
export RUSTC="$g/sysroot/usr/lib/rust-1.93/bin/rustc"
export GFXSTREAM_PATH="$k/new-gfx-gles-build/host"
(cd "$build/src" && timeout 600 "$g/sysroot/usr/lib/rust-1.93/bin/cargo" \
  build --locked --offline -p kumquat_virtio --features gfxstream -j2)
echo "server=$build/target/debug/kumquat"
```

把打印出的服务器路径用于上面的修正版 `--server` 可复验；保持 `--locked --offline`。
