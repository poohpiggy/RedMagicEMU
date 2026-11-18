# gfxstream ColorBuffer 缺少 GL backing 的研究补丁

这里保存独立研究材料，**尚未集成到 RedMagicEMU**。补丁针对
[google/gfxstream 的固定提交 cf47bd2660e7217f5729c91664d36d81c1b3282a](https://github.com/google/gfxstream/blob/cf47bd2660e7217f5729c91664d36d81c1b3282a/host/color_buffer.cpp)。
上游文件版权属于 Android Open Source Project，采用 [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)；测试源码也使用 Apache-2.0。仓库中没有复制上游源码或二进制。

原版 `ColorBuffer::Impl::updateGlFromBytes` 在没有 GL backing 时返回 `true`，即使没有执行更新。补丁让此分支返回 `false`；其余路径不变。测试创建没有 GL、Vulkan backing 的 ColorBuffer，检查现有的无须同步路径仍返回成功，并检查 GL 更新必须失败。

此测试**没有创建或验证真实 GLES 后端**，不能证明安兔兔黑屏、图形显示或 Android GPU 已修复。集成前必须先审计 ColorBuffer 和 backing 的生命周期、调用方的错误处理，并覆盖有真实 GL backing、更新成功及底层更新失败的情况。

## 本机已构建库的复验

以下命令在 WSL 使用本次研究的临时目录，不下载或覆盖上游源码。它们依赖此前在本机隔离构建的 Linux x86-64 原版和修正版库，不是 Android 构建。`REPO` 指向本工作区。

```bash
set -e
REPO=/mnt/e/RedMagicEMU
K=/var/tmp/rme-kumquat-research.r6Wg21lO
G=/var/tmp/rme-gfxstream-build.hTnuwsWV
R=/var/tmp/rme-gfxstream-cf47-android.lVju0gMk
TEST="$REPO/tools/diagnostics/gfxstream-color-buffer"

git -C "$K/google-gfxstream-cf47" apply --check "$TEST/missing-gl-backing.patch"
cd "$K/new-gfx-gles-build"
"$G/bin/cxx" -std=gnu++17 -DGFXSTREAM_ENABLE_HOST_GLES=1 \
  -I../google-gfxstream-cf47/host \
  -I../google-gfxstream-cf47/host/include \
  -I../google-gfxstream-cf47/host/common/include \
  -I../google-gfxstream-cf47/common/base/include \
  -I../google-gfxstream-cf47/third_party/vulkan/include \
  -I../google-gfxstream-cf47/third_party/opengl/include \
  "$TEST/color-buffer-regression.cpp" host/libgfxstream_backend.so.0.1.2 \
  -o "$R/color-buffer-recheck"

set +e
LD_LIBRARY_PATH="$K/new-gfx-gles-build/host:$G/sysroot/usr/lib/x86_64-linux-gnu" \
  "$R/color-buffer-recheck"
baseline=$?
LD_LIBRARY_PATH="$R/build-glfix/host:$G/sysroot/usr/lib/x86_64-linux-gnu" \
  "$R/color-buffer-recheck"
fixed=$?
printf 'baseline=%s fixed=%s\n' "$baseline" "$fixed"
test "$baseline" -eq 1 && test "$fixed" -eq 0
```

本次复验的原版输出 `gl_update_without_backing=1`，退出码 1；修正版输出 `gl_update_without_backing=0`，退出码 0。两者都输出 `no_backing_syncs=1`。固定源码归档 SHA-256 为 `cb3e512211701fb55dfcb94685945203db56656cee096981ca4ad66e1d463189`；原版和修正版源码目录对比仅 `host/color_buffer.cpp` 不同。
