# clvk 静态 local 内存报告回归

这是电脑端独立研究，**没有修复或接入产品，也没有修改 Guest 或设备**。固定研究版本为 clvk `a56c4a442a4671fc08478ab39463a1da28e4a818` 和实验中使用的 clspv `545a65b5f4fda9a7c285bb7fd75eda86aa2d853b`。上游 clvk 源码采用 Apache-2.0；本目录的测试和脚本采用 MIT，不包含上游源码或二进制。

OpenCL 的 [`CL_KERNEL_LOCAL_MEM_SIZE` 规定](https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_API.html)包括内核内的 `__local` 变量以及通过 `clSetKernelArg` 指定的 local 参数。固定 clvk 的 [`src/kernel.cpp:22`](https://github.com/kpet/clvk/blob/a56c4a442a4671fc08478ab39463a1da28e4a818/src/kernel.cpp#L22) 有 FIXME：当前实现只累计 local 参数，漏掉编译期分配。

`probe.c` 在线编译一个含两个 kernel 的 OpenCL C 程序，只查询属性，**不提交计算**：

- `fixed_dynamic` 使用静态 `__local uint[16]`，因此未设置动态参数时至少应报告 64 字节；设置第二个参数为 96 字节后至少应报告 160 字节。源中通过 helper 访问静态数组。
- `vector_three` 使用 `__local uint3[7]`。OpenCL 的三分量向量具有四分量大小和对齐，因此至少需要 112 字节，不能按 84 字节估算。

这是下限测试，不保证报告包含实现内部的所有内存，也不证明通用修复正确。源级 helper 在本次编译中被内联，测试不能证明所有可达函数形式都得到正确处理。

## 重现当前失败

以下命令使用已存在的 WSL 隔离构建，不下载组件。脚本创建新的私有临时目录并保存日志，保留探针原始退出码；**当前问题下预期输出 FAIL 并退出 1**。退出 2 表示构建或 API 等其他错误，不能当成此回归已复现。

```sh
C=/var/tmp/rme-clvk-build-dEVbhfMR
bash /mnt/e/RedMagicEMU/tools/diagnostics/clvk-local-memory-report/run.sh \
  "$C/build2" "$C/build2/clspv" "$C/clvk/external/OpenCL-Headers" \
  "$C/sysroot/usr/lib/x86_64-linux-gnu" /usr/share/vulkan/icd.d/lvp_icd.json
```

本次 llvmpipe CPU 软件后端实测：`before=0`、`after=96`、`vector_three reported=0`，低于上述下限，探针退出 1。该 clspv 构建的 GCC 15 兼容头注入记录在 `/var/tmp/rme-clvk-build-dEVbhfMR/MANIFEST.txt`。

## 为什么没有提交通用修复

本次捕获的多 kernel SPIR-V 1.6 含各自的静态 Workgroup 数组和一个动态 local 参数的特化数组。固定 clspv 将所有 local 全局变量**保守地加入**入口 interface；一个 kernel 的 interface 中也列出了另一个 kernel 的静态数组。因而按整个模块或入口 interface 求和不能得到精确的逐 kernel 静态声明总量，动态特化数组还可能与现有 local 参数计数重复。较大的查询值本身不能证明违反规范：该查询还允许包含实现所需的 local 内存。

固定 clspv 的 `SPIRVProducerPass.cpp` 为动态参数生成 SpecId，现有 reflection 的 `ArgumentWorkgroup` 提供参数元素大小与 SpecId，但未发现每个 kernel 的静态 local 总量字段。复合类型还需要遵守实际类型布局和对齐；本次 `uint3[7]` 的捕获结果体现为七个四分量 32 位向量。源级 noinline helper 在当前编译配置被内联，不能假定其他模块也没有跨函数可达性。

可靠方案需要对入口点可达性和共享变量去重、复合类型及溢出、动态参数及特化常量、不同 SPIR-V 版本、二进制加载路径和 reflection 格式版本做完整设计与测试，并考虑实现内部内存。当前没有足够证据提供通用、准确的补丁，故只保留失败回归和分析，不填充猜测值。

## 较新上游的隔离复验

另行固定官方 clvk [`9862fce7d1b9b68a45986ef5e15b2fb81e417246`](https://github.com/kpet/clvk/tree/9862fce7d1b9b68a45986ef5e15b2fb81e417246) 和其配套的 clspv [`b36e501783d336e8ba2fd26c6832d6d6faf74d5d`](https://github.com/google/clspv/tree/b36e501783d336e8ba2fd26c6832d6d6faf74d5d)，在电脑的隔离目录从源码构建。该版本的 [`kernel.cpp`](https://github.com/kpet/clvk/blob/9862fce7d1b9b68a45986ef5e15b2fb81e417246/src/kernel.cpp#L24) 把编译器反射的静态 Workgroup 大小与动态参数大小相加；[`program.cpp`](https://github.com/kpet/clvk/blob/9862fce7d1b9b68a45986ef5e15b2fb81e417246/src/program.cpp#L320) 将反射项累计为程序级总量。相应的 clspv [`SPIRVProducerPass.cpp`](https://github.com/google/clspv/blob/b36e501783d336e8ba2fd26c6832d6d6faf74d5d/lib/SPIRVProducerPass.cpp#L8143) 输出变量 ID 和大小，没有在该反射项中指定 kernel。

WSL 上的 llvmpipe CPU 后端实测 `fixed_dynamic before=176`、设置 96 字节动态参数后 `after=272`，`vector_three reported=176`，本测试下限通过，退出 0。这些数值与两个静态数组 64 + 112 字节的程序级汇总一致；本次测试不能证明每个 kernel 的精确静态用量，也不能仅凭高于声明值判定违反 OpenCL 规范。原固定版本的失败基线保持不变。

下列命令仅复用本次已有的临时构建，不下载或修改上游源码；临时目录不保证长期存在。探针每次创建新目录和新进程，禁用 clvk 磁盘 pipeline 缓存及 Mesa shader 磁盘缓存；clvk 仍可在进程内缓存。此实验没有接入 EMU，未在 Android 或手机 GPU 验证，测试通过不代表完整 OpenCL 支持。

```sh
R=/var/tmp/rme-upstream-local-research.p1xQsbd3
C=/var/tmp/rme-clvk-build-dEVbhfMR
bash /mnt/e/RedMagicEMU/tools/diagnostics/clvk-local-memory-report/run.sh \
  "$R/build" "$R/build/clspv" "$R/source/clvk/external/OpenCL-Headers" \
  "$C/sysroot/usr/lib/x86_64-linux-gnu" /usr/share/vulkan/icd.d/lvp_icd.json
```

构建脚本和日志分别为 `$R/configure.sh`、`$R/configure.log`、`$R/build.sh`、`$R/build.log`、`$R/build-j4.sh` 和 `$R/build-j4.log`；源码归档哈希在 `$R/archive-hashes.txt`。原构建由 -j2 有界停止后以 -j4 续建，最终退出 0。新版 `clspv` 的 SHA-256 为 `8c145ba2642a460612772e5ff464c59fb6268e6daae0232db6eb0f60e9b25176`，`libOpenCL.so.0.1` 为 `26d09c254e6ba9a9951d43c86b7c082ee5cc8bd290819b6e7c2f10c2a3cb6756`；运行时还依赖系统只读 Vulkan loader 和上述隔离目录中的库。
