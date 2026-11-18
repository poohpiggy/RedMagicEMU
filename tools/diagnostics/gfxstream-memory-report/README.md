# gfxstream device-memory-report 研究

这是独立研究补丁，**未接入 RedMagicEMU**。它针对 Mesa 固定提交
[`6e4a77089e1db6cb6269ec694de3a1a131f96689`](https://gitlab.freedesktop.org/mesa/mesa/-/commit/6e4a77089e1db6cb6269ec694de3a1a131f96689)
中的 `src/gfxstream/guest/vulkan_enc/ResourceTracker.cpp`。该上游文件的版权属于 Google，采用 MIT 许可证；本目录测试源码和脚本也采用 MIT 许可证。本目录不包含上游源码或二进制。

固定版本以测试兼容为由无条件通告 `VK_EXT_device_memory_report`，并将对应 feature 设为 true。客户端确实实现了部分显式设备内存分配、导入和释放回调，但代码未展示完整的宿主内部内存事件回传，部分失败路径也没有发出失败回调。[Vulkan 扩展说明](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_device_memory_report.html)要求报告应用通常无法直接看到的设备内存使用。因此，在完整语义得到实现和验证之前，研究补丁移除该扩展的透传和强制通告，并将 feature 设为 false；它**没有实现该扩展**。

## 测试范围

`probe.c` 用真实 Vulkan loader 和客户端检查原版报告扩展可用并可创建设备，修正版报告扩展不可用、feature 为 false、请求启用扩展返回 `VK_ERROR_EXTENSION_NOT_PRESENT`。脚本还比较其余设备扩展及版本、核心 feature、队列属性和设备 API，并使用相邻的 [Vulkan 计算探针](../vulkan-compute/README.md)在两版上各执行 64 项校验及预期错误的负对照。

比较仅覆盖上述枚举值和这个计算探针，不能证明其他全部能力或 Vulkan 一致性。这里使用 Linux x86-64、llvmpipe 软件后端；**没有 Android 实机、真实 GPU、EMU 接入、AHardwareBuffer 或图形显示验证**。保持原有 ROM 和 Guest。

## 复验现有隔离构建

以下路径是本次实验已在 WSL 中准备好的固定源码、依赖和库，不会下载内容。脚本使用独立 user/mount/PID namespace、进程组和自己的临时 socket；结果保存在脚本打印的 `/var/tmp/rme-memory-report-check.*` 中。正常清理记录服务进程真实退出码；无响应时限时发送 SIGKILL 并报告失败。实验进程组有 300 秒总时限。需要 WSL 支持 `unshare -Urnmpf --kill-child=KILL`，以及 C 编译器、Python 3、Vulkan loader 和 NDK 的 Linux shader 工具。

```bash
REPO=/mnt/e/RedMagicEMU
K=/var/tmp/rme-kumquat-research.r6Wg21lO
G=/var/tmp/rme-gfxstream-build.hTnuwsWV
R=/var/tmp/rme-memory-report.4T58y3vt
NDK=/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c

bash "$REPO/tools/diagnostics/gfxstream-memory-report/verify.sh" \
  "$K/mesa-build-pinned2/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.x86_64.json" \
  "$R/patched-icd.json" "$K/target-9f22/debug/kumquat" \
  "$K/new-gfx-gles-build/host" "$G/sysroot/usr/lib/x86_64-linux-gnu" \
  "$NDK" "$K/google-gfxstream-cf47/third_party/vulkan/include"
```

本次复验原版为 `ext=1 feature=1`、`CREATE_DEVICE_REPORT=0`；修正版为 `ext=0 feature=0`、`CREATE_DEVICE_REPORT=-7`。两版普通计算和故意引入不匹配的负对照通过，其余所列能力比较一致。

清理路径还可用自建假服务做回归：向与上面同样的七个参数调用 `test-lifecycle.py`。它检查忽略 SIGTERM 或被暂停的服务被限时结束且保留原始客户端错误，并检查外层分别收到 SIGTERM 和 SIGKILL 后没有存活的测试服务：

```bash
python3 "$REPO/tools/diagnostics/gfxstream-memory-report/test-lifecycle.py" \
  "$K/mesa-build-pinned2/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.x86_64.json" \
  "$R/patched-icd.json" "$K/target-9f22/debug/kumquat" \
  "$K/new-gfx-gles-build/host" "$G/sysroot/usr/lib/x86_64-linux-gnu" \
  "$NDK" "$K/google-gfxstream-cf47/third_party/vulkan/include"
```

## 从固定源码重建修正版

需要现存的固定 Mesa 源码及其已准备的离线子项目、上面的构建工具和原版客户端；不自动同步上游。下面命令创建新的隔离副本，应用本目录补丁并限制构建并发。固定 Mesa 源码归档 SHA-256：`f342b7b1fc69a51a389cd11a3fb92d61a4d84bd670aff503e436d027dffdb63b`。

```bash
set -e
REPO=/mnt/e/RedMagicEMU
K=/var/tmp/rme-kumquat-research.r6Wg21lO
G=/var/tmp/rme-gfxstream-build.hTnuwsWV
R2=$(mktemp -d /var/tmp/rme-memory-rebuild.XXXXXXXX)
cp -a --reflink=auto "$K/mesa-6e4a" "$R2/mesa-patched"
git -C "$R2/mesa-patched" apply --check \
  "$REPO/tools/diagnostics/gfxstream-memory-report/hide-incomplete-report.patch"
git -C "$R2/mesa-patched" apply \
  "$REPO/tools/diagnostics/gfxstream-memory-report/hide-incomplete-report.patch"
(
  export PATH="$K/mesa-tools/root/usr/bin:$G/sysroot/usr/lib/rust-1.93/bin:$G/sysroot/usr/bin:$PATH"
  export PYTHONPATH="$G/sysroot/usr/lib/python3/dist-packages"
  export LD_LIBRARY_PATH="$G/sysroot/usr/lib/rust-1.93/lib:$G/sysroot/usr/lib/x86_64-linux-gnu"
  export PKG_CONFIG_PATH="$G/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig"
  export CXX="$G/bin/cxx"
  "$G/sysroot/usr/bin/meson" setup "$R2/build" "$R2/mesa-patched" \
    -Dvulkan-drivers=gfxstream -Dgallium-drivers= -Dvirtgpu_kumquat=true \
    -Dopengl=false -Dplatforms= -Dbuild-tests=false -Dllvm=disabled \
    -Dxmlconfig=disabled -Dexpat=disabled -Dauto_features=disabled -Dwrap_mode=nodownload \
    "-Dc_args=-I$G/sysroot/usr/include -I$G/sysroot/usr/include/libdrm" \
    "-Dcpp_args=-I$G/sysroot/usr/include -I$G/sysroot/usr/include/libdrm"
  "$G/sysroot/usr/bin/ninja" -C "$R2/build" -j2 src/gfxstream/guest/vulkan/libvulkan_gfxstream.so
)
python3 - "$K/mesa-build-pinned2/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.x86_64.json" "$R2" <<'PY'
import json, pathlib, sys
manifest = json.loads(pathlib.Path(sys.argv[1]).read_text())
root = pathlib.Path(sys.argv[2])
manifest["ICD"]["library_path"] = str(root / "build/src/gfxstream/guest/vulkan/libvulkan_gfxstream.so")
(root / "patched-icd.json").write_text(json.dumps(manifest, indent=2) + "\n")
PY
echo "Use $R2/patched-icd.json as the second verify.sh argument."
```
