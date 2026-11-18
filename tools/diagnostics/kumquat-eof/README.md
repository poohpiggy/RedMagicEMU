# Kumquat 连接 EOF 回归实验

这是电脑端的隔离研究，**没有集成到 EMU，也没有安装到设备**。固定研究对象为上游 [rutabaga_gfx 的 9f22cc994019486a03086992609c0aa4769ae4b0](https://github.com/magma-gpu/rutabaga_gfx/tree/9f22cc994019486a03086992609c0aa4769ae4b0)，其许可证为 [BSD-3-Clause](https://github.com/magma-gpu/rutabaga_gfx/blob/9f22cc994019486a03086992609c0aa4769ae4b0/LICENSE)。本目录只保存小型补丁和自行编写的测试，不包含上游完整源码或二进制。

原版事件循环将 `process_command` 的 EOF 返回值取反：连接关闭后，服务端 FD 仍保留。补丁只修正 EOF 与 HUP 判断：处理可读数据时以实际 EOF 作为结束条件；只有 HUP 且无可读数据时关闭连接。它不改变 capset、对象所有权或其他错误处理。

## 限制

- 补丁**只回收连接 FD**。context、resource 和 fence 的归属及断线回收未解决；已发现跨连接 context 操作、资源共享凭据不足和资源关联记录不完整。此实验不能作为完整连接隔离或产品可用的证明。
- 上游 gfxstream 的 suspend/resume 仍有 TODO；本实验不测试状态保存。
- 服务端通过用户和挂载命名空间使用独立的 `/tmp`；测试产物和日志保存在新建的 `/var/tmp/rme-kumquat-eof-*`，不会覆盖共享 socket。结束时向服务端发送 SIGTERM，`server_exit_after_test=-15` 表示信号终止，**不表示服务端优雅退出**。
- 验证使用 llvmpipe CPU 后端及 `VulkanExternalMemoryMode:HostAllocation,SystemBlob:enabled`。上游警告 SystemBlob 在非 Windows 上尚未充分测试；不能推断 Android 或 Adreno 行为。
- `submit-gate.c` 只在测试中暂停已有客户端的真实 `vkQueueSubmit`，解除暂停后调用真正的 Vulkan loader。它不修改驱动或生产程序。

## 当前电脑上的原版和修正版回归

以下命令依赖本次实验保存在 `/var/tmp` 的固定组件：gfxstream `cf47bd2660e7217f5729c91664d36d81c1b3282a`、Mesa `6e4a77089e1db6cb6269ec694de3a1a131f96689`、相应构建产物和 `tools/diagnostics/vulkan-compute` 探针。临时目录失效后，需先重建这些组件；这里不提供未知来源的预编译文件。

在 WSL 的同一个 shell 执行：

```sh
k=/var/tmp/rme-kumquat-research.r6Wg21lO
g=/var/tmp/rme-gfxstream-build.hTnuwsWV
d=/var/tmp/rme-gfxstream-cf47-android.lVju0gMk
t=/mnt/e/RedMagicEMU/tools/diagnostics/kumquat-eof

python3 "$t/run.py" \
  --server "$k/target-9f22/debug/kumquat" \
  --probe "$k/probe-system-icd" \
  --client-icd "$k/mesa-build-pinned2/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.x86_64.json" \
  --host-icd /usr/share/vulkan/icd.d/lvp_icd.json \
  --vulkan-include "$k/google-gfxstream-cf47/third_party/vulkan/include" \
  --library-path "$k/new-gfx-gles-build/host:$g/sysroot/usr/lib/x86_64-linux-gnu"
echo "original_exit=$?"

python3 "$t/run.py" \
  --server "$d/target-eoffix/debug/kumquat" \
  --probe "$k/probe-system-icd" \
  --client-icd "$k/mesa-build-pinned2/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.x86_64.json" \
  --host-icd /usr/share/vulkan/icd.d/lvp_icd.json \
  --vulkan-include "$k/google-gfxstream-cf47/third_party/vulkan/include" \
  --library-path "$k/new-gfx-gles-build/host:$g/sysroot/usr/lib/x86_64-linux-gnu"
echo "fixed_exit=$?"
```

测试要求：10 次空连接关闭、10 次查询后关闭、10 次排队两条完整查询再关闭写端后，服务端 FD 数恢复到基线；排队消息 10/10 均得到两条响应。一个在测试前已创建 Vulkan 设备的客户端在测试后继续执行 64 项计算，新客户端也必须完成同样计算；每个客户端必须退出 0 且输出 `VULKAN_COMPUTE_OK words=64`。

本机原版退出 1，FD 为 `8 → 18 → 28 → 38`；修正版退出 0，FD 始终为 `8`，排队消息 10/10，两个客户端均退出 0。具体 PID、结果和日志目录以每次输出为准。最初失败日志为 `/var/tmp/rme-eof-original.C0sjXy`，修正版独立日志为 `/var/tmp/rme-eof-fixed.OVrXlx`。

## 超时清理

主测试在新进程组中启动命名空间，确认服务端与客户端继承该组。100 秒超时时，仅向这个测试组发送 SIGTERM，等待 0.25 秒后发送 SIGKILL；启动进程在发完信号前不会被回收，避免其 PID 被复用成无关进程组。测试使用自建的、忽略 SIGTERM 的后代，以及另一个独立进程组的哨兵，不会向其他进程发信号：

```sh
python3 "$t/timeout-cleanup-test.py" --baseline  # 预期退出 1：旧处理留下后代
python3 "$t/timeout-cleanup-test.py"             # 预期退出 0：后代终止、哨兵仍存活
```

## 纯 HUP 分支的合成覆盖

本机真实 socket 关闭产生的 epoll 标志为 `IN|HUP`，并未自然观察到单独 HUP。以下 GDB 测试在一次真实挂断事件上将 `readable` 设为 false，验证仅 HUP 分支实际执行、连接 FD 回到基线，并在随后运行真实 Vulkan 计算。它是**调试器注入的分支覆盖**，不能声称系统自然产生过 HUP-only。

```sh
RME_OUT=$(mktemp -d /var/tmp/rme-eof-hup.XXXXXX)
RME_PARENT_MNT=$(readlink /proc/self/ns/mnt)
RME_GDB="$g/sysroot/usr/bin/gdb"
RME_SERVER="$d/target-eoffix/debug/kumquat"
RME_PROBE="$k/probe-system-icd"
RME_HOST_ICD=/usr/share/vulkan/icd.d/lvp_icd.json
RME_CLIENT_ICD="$k/mesa-build-pinned2/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.x86_64.json"
RME_LIB_PATH="$k/new-gfx-gles-build/host:$g/sysroot/usr/lib/x86_64-linux-gnu"
export RME_OUT RME_PARENT_MNT RME_GDB RME_SERVER RME_PROBE RME_HOST_ICD RME_CLIENT_ICD RME_LIB_PATH
echo "$RME_OUT"
timeout -k 2 40 unshare --user --map-root-user --mount --propagation private \
  bash "$t/hup-only-run.sh"
echo "hup_test_exit=$?"
```

GDB 脚本依赖该固定源码的调试信息与行号。本机结果是 `simulated_hup_only_branch=yes`、FD `7 → 7`、`subsequent_compute=pass`，退出 0；记录位于 `/var/tmp/rme-eof-hup.g229mg`。

## 从本次固定源码重建修正版

此步骤只在新临时目录复制固定源码，不安装、不修改原始树；`Cargo.lock` 已在本次实验解析，其 SHA-256 为 `7a786791e5c1eaea93204b3941d6d00635888fc8d35d03362a862e6dfe6a596d`。离线构建要求相应 crate 已在本次实验的 Cargo 缓存中。

```sh
build=$(mktemp -d /var/tmp/rme-kumquat-eof-build.XXXXXX)
cp -a "$k/rutabaga-9f22" "$build/src"
(cd "$build/src" && patch -p1 < "$t/eof-lifecycle.patch")
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

用打印出的服务器路径替换上面的 `--server` 可复验；保持 `--locked --offline`，不要覆盖原始源码和产物。
