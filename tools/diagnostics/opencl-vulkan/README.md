# Standalone OpenCL source and compute probe

`probe.c` enumerates OpenCL devices, prefers a reported GPU, prints the chosen
device name, OpenCL-reported type and compiler availability, then compiles an
OpenCL C kernel at runtime. It sends 64 input words, computes `input * 3 + 7`,
reads the output and verifies every word. OpenCL errors and build logs go to
stderr; a failure returns nonzero. Resources are released before exit.

The device type is what the OpenCL implementation reports; it is not independent
proof of GPU acceleration. This is an isolated diagnostic, not an EMU OpenCL or
Vulkan bridge, and does not establish Geekbench compatibility.

`device-info.c` separately enumerates OpenCL platforms and devices, reporting
versions, image and compiler support, FP16 and workgroup limits. It creates no
OpenCL context or queue and does not execute a kernel. The reported properties
do not by themselves prove an application namespace or workload works.
It also reports whether a small, named set of external-memory and vendor host
pointer extensions is present; an extension name is not an interoperability test.

`--workgroup` runs a second kernel: four workgroups of 16 items each sum
their input through a `__local` array and barriers. Every group is compared
with a host-computed result. Combine it with `--expect-mismatch` to exercise
the failure path. The probe reports the queried kernel local-memory size;
the experimental clvk revision reports zero even for this kernel. Its
`src/kernel.cpp` explicitly omits compile-time local allocations, so this
number is not evidence of the actual local-memory usage.

## Computer-only run

The following commands were used on 2026-11-16 with the isolated clvk build
already present in WSL. These `/var/tmp` paths are local, temporary build
artifacts, not checked-in dependencies. The clvk commit was
`a56c4a442a4671fc08478ab39463a1da28e4a818`, clspv was
`545a65b5f4fda9a7c285bb7fd75eda86aa2d853b`; that host build uses its
separately built `clspv` executable for source compilation.

```sh
BASE=/var/tmp/rme-clvk-build-dEVbhfMR
OUT=/var/tmp/rme-opencl-probe.UsgOfqdI
SRC=/mnt/e/RedMagicEMU/tools/diagnostics/opencl-vulkan/probe.c
cc -std=c11 -Wall -Wextra -Werror -O2 \
  -I "$BASE/clvk/external/OpenCL-Headers" "$SRC" \
  -L "$BASE/build2" -lOpenCL -o "$OUT/probe-linux"
export LD_LIBRARY_PATH="$BASE/build2:$BASE/sysroot/usr/lib/x86_64-linux-gnu"
export CLVK_CLSPV_PATH="$BASE/build2/clspv"
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
timeout 60 "$OUT/probe-linux" --expect-mismatch
timeout 60 "$OUT/probe-linux" --bad-kernel
timeout 60 "$OUT/probe-linux"
```

The first command intentionally checks against an incorrect expected value; it
returned 1 with `intentional expectation mismatch`. The second intentionally
uses invalid OpenCL C; it returned 1 and reported `clBuildProgram` error `-11`
with the compiler log. The final command returned 0 and printed
`OPENCL_COMPUTE_OK words=64`. All three reported llvmpipe as a CPU OpenCL device
with the source compiler available. The outputs are software CPU results, not
physical GPU tests. Each command has a process timeout because an OpenCL driver
call may otherwise block indefinitely.

## Android cross-build only

The following used the separately cross-built, in-process-compiler clvk library;
its `CLVK_ENABLE_SPIRV_IL` was disabled due to the pinned translator/LLVM
version mismatch. This does not disable OpenCL C source compilation.

```sh
NDK=/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
CLVK=/var/tmp/rme-clvk-android-noil.rxik0nVG/libOpenCL.strip-unneeded.so
"$BIN/clang" --target=aarch64-linux-android29 -std=c11 \
  -Wall -Wextra -Werror -O2 -Wl,-z,max-page-size=16384 \
  -I "$BASE/clvk/external/OpenCL-Headers" "$SRC" "$CLVK" \
  -o "$OUT/probe-android-arm64"
sha256sum "$OUT/probe-android-arm64"
"$BIN/llvm-readelf" -h -l -d "$OUT/probe-android-arm64"
```

The test executable with workgroup mode has SHA-256
`2c3ece5a64f105cbc47dd43e020e68ea97bf2cc14abac819339d2a53757dfb17`;
the older build without workgroup mode has SHA-256
`13b4f88b898770ce294d3e99aed3e4be9402f697ced9a4e6451e2126e10631e9`.
It is AArch64, requests `/system/bin/linker64`, has 16 KiB LOAD alignment,
and needs `libOpenCL.so`, `libdl.so` and `libc.so`. The clvk build was linked
for this check only. The workgroup probe was later run on the host phone using
its existing Qualcomm OpenCL library, not this clvk build; neither was
installed as an APK or connected to EMU.

### Newer clvk Android research build

The newer clvk `9862fce7d1b9b68a45986ef5e15b2fb81e417246` and its pinned
clspv `b36e501783d336e8ba2fd26c6832d6d6faf74d5d` were also cross-built in
`/var/tmp/rme-clvk-new-android.7nWVMImi`. This experimental API 29 ARM64 build
enables the in-process OpenCL C compiler and disables SPIR-V IL. It uses the
fixed gfxstream Vulkan 1.4.363 headers because NDK r28c's older headers do not
compile the new device source. The upstream `CLSPV_EXTERNAL_LIBCLC_DIR` option
reuses SPIR-V bitcode built from the matching fixed LLVM on the computer.

The strip-debug library is stored outside Git at
`build/diagnostics/opencl-vulkan/libOpenCL-arm64-api29-newclvk-experimental-a3c729d8.so`;
its size is 97,543,200 bytes and SHA-256 is
`a3c729d81a07da65a50213e787569ce0205cea4d16746d09939b5b5ea9554430`.
It has AArch64 LOAD segments aligned to 16 KiB and needs `libvulkan.so`,
`liblog.so`, `libdl.so`, `libm.so`, `libz.so`, `libc++_shared.so` and `libc.so`.
The existing workgroup probe linked against it with strict undefined-symbol
checks. A separate static check found candidate NDK API 29 exports for the
strong undefined symbols in the probe, library and C++ runtime; weak symbols,
actual loader namespace visibility and execution were not verified.

The same library was compared with NDK API 26 stubs: six strong symbols have
no candidate there, including `vkGetPhysicalDeviceFeatures2` and five
`posix_spawn` functions; the first candidate stubs appear at API 28. The ELF
was built for API 29, so this comparison does not establish that it can run on
API 28 either. The relevant system libraries are those visible to the process
that actually loads the library; no particular EMU loading location is assumed.

This is a research artifact, not an EMU or APK asset. The included upstream
dependencies and their licenses require review before distribution. The build,
input hashes, probe link and symbol-check scripts remain in the temporary
directory; they are not a self-contained reproduction package. This experimental
clvk library has not been run on Android or a physical GPU.

The separate `device-info.c` can be cross-built with an existing NDK, OpenCL
headers and a link-time library that provides the `libOpenCL.so` SONAME:

```sh
N=/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin
H=/var/tmp/rme-upstream-local-research.p1xQsbd3/source/clvk/external/OpenCL-Headers
L=/var/tmp/rme-clvk-new-android.7nWVMImi/libOpenCL.strip-debug.so
"$N/clang" --target=aarch64-linux-android29 -std=c11 -Wall -Wextra -Werror -O2 \
  -I "$H" /mnt/e/RedMagicEMU/tools/diagnostics/opencl-vulkan/device-info.c "$L" \
  -Wl,--no-undefined -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384 \
  -o /path/to/new/private-folder/device-info
```

On 2026-11-17 the user's host phone ran the information and workgroup probes
using its existing Qualcomm OpenCL library from the shell. It reported Adreno
750, OpenCL 3.0, OpenCL C 3.0, image and compiler availability, and
`cl_khr_fp16`; the separate compute and workgroup checks and their negative
controls passed. The temporary probes were removed. These results do not
demonstrate that EMU or an Android app can access the same library in its own
namespace, or that Geekbench is compatible.

The host OpenCL device also reported `cl_qcom_ext_host_ptr` and
`cl_qcom_android_native_buffer_host_ptr`; it did not report
`cl_khr_external_memory` or `cl_khr_external_memory_android_hardware_buffer`.
No OpenCL/native-buffer import or cross-process transfer was performed.

## Isolated clvk → gfxstream → Kumquat experiment

`run-kumquat.sh` connects the source-compiler probe to an already built clvk,
Mesa gfxstream client and Kumquat server. It downloads nothing and takes all
component paths as arguments. It uses the bounded process-group and private
user/mount/PID namespace supervisor from
[`gfxstream-memory-report`](../gfxstream-memory-report/README.md), creates a
new private output directory and socket for each run, and uses the server
features `VulkanExternalMemoryMode:HostAllocation,SystemBlob:enabled`.

The test checks a missing server, invalid OpenCL C, intentionally wrong
expected results, a 64-word computation and the workgroup reduction. It records the input
binary hashes and traces `execve`, socket connection, `sendmsg` and
`recvmsg`; the successful trace must show the actual clspv invocation and
private Kumquat socket traffic. The server must remain alive between tests.
The final server exit 143 is a **SIGTERM forced stop**, not graceful shutdown.
Logs and traces are kept in the printed private directory; they can include
command payloads and local paths.

The following uses the already prepared WSL research artifacts. These paths
are temporary local artifacts, not bundled dependencies:

```sh
K=/var/tmp/rme-kumquat-research.r6Wg21lO
C=/var/tmp/rme-clvk-build-dEVbhfMR
G=/var/tmp/rme-gfxstream-build.hTnuwsWV
timeout --foreground -k 3 180 bash /mnt/e/RedMagicEMU/tools/diagnostics/opencl-vulkan/run-kumquat.sh \
  "$C/build2" "$C/build2/clspv" "$C/clvk/external/OpenCL-Headers" \
  "$K/mesa-build-pinned2/src/gfxstream/guest/vulkan/gfxstream_vk_devenv_icd.x86_64.json" \
  "$K/target-9f22/debug/kumquat" "$K/new-gfx-gles-build/host" \
  "$K/strace-tools/root/usr/lib/x86_64-linux-gnu:$C/sysroot/usr/lib/x86_64-linux-gnu:$G/sysroot/usr/lib/x86_64-linux-gnu" \
  "$K/strace-tools/root/usr/bin/strace" /usr/share/vulkan/icd.d/lvp_icd.json
```

The tested inputs used Kumquat commit `9f22cc994019486a03086992609c0aa4769ae4b0`
with Cargo.lock SHA-256
`7a786791e5c1eaea93204b3941d6d00635888fc8d35d03362a862e6dfe6a596d`,
gfxstream commit `cf47bd2660e7217f5729c91664d36d81c1b3282a`, Mesa commit
`6e4a77089e1db6cb6269ec694de3a1a131f96689`, and the clvk/clspv commits
listed above. The clspv build included a GCC 15 compatibility header injection,
documented in `/var/tmp/rme-clvk-build-dEVbhfMR/MANIFEST.txt`.

In the computer run, clvk reported a virtual GFXStream device as OpenCL
`type=GPU`; the server selected **llvmpipe**, a CPU software Vulkan backend.
The label is not evidence of a physical GPU. This is a local experiment, not
an EMU integration or an Android/Geekbench test.

## Existing clvk simple_test runner

`run.py` exercises an already-built upstream clvk `simple_test` with explicitly
supplied test, library, compiler, and Vulkan ICD paths. It downloads and installs
nothing. It uses a private temporary directory, disables caches, and requires
both the missing-compiler failure and the verified successful result:

```sh
python3 tools/diagnostics/opencl-vulkan/run.py \
  --test /path/to/build/simple_test \
  --library-dir /path/to/build \
  --clspv /path/to/build/clspv \
  --icd /usr/share/vulkan/icd.d/lvp_icd.json
python3 tools/diagnostics/opencl-vulkan/test_runner.py
```

The runner trusts the supplied binaries and reports their device name; it does
not prove their provenance or device type. The clvk commit above originally
pins clspv `c8c27e92503d8224a1b1f10e8a4491165222f20c`; this experiment explicitly
used the separately published clspv commit above. It is not a conformance test.
