# Native OpenCL bridge on Windows

The native worker receives OpenCL source, buffers, and kernel arguments from a local test client. It compiles and runs the kernel through the vendor ICD selected by Windows `OpenCL.dll`. The benchmark also calls the same C dispatcher in-process to establish the native baseline. The worker uses inherited anonymous pipes. With shared replies, it writes readback bytes into an inherited anonymous mapping and sends only the reply header over the pipe.

## Build and verify

Use 64-bit Windows Python 3.10+, Android NDK r28c, and the Khronos OpenCL-Headers checkout already used to build clvk (`52db34e323958d9b28d0bbe198d57e1ca5316c71`). `-OpenClHeaders` takes the directory containing `CL/cl.h`.

```powershell
$ndk = "$env:USERPROFILE/.redmagicemu-tools/android-sdk/ndk/28.2.13676358"
$headers = 'C:/path/to/OpenCL-Headers'
$out = 'build/diagnostics/opencl-native'
./native/openclbridge/build.ps1 -Ndk $ndk -OpenClHeaders $headers -Output $out
python native/openclbridge/verify.py --build $out --device 'NVIDIA GeForce RTX 3080' --samples 60
```

Each timed dispatch checks all output words against a CPU reference. The harness also expects a real driver build log for intentionally invalid source, then keeps running the previously compiled kernel. It checks that readback from a released buffer reports `CL_INVALID_MEM_OBJECT`. The timed interval covers warm submission, synchronization, and readback. It excludes context creation, compilation, and the initial upload. The GPU column comes separately from native OpenCL event timestamps.

## Protocol

Requests are three little-endian 32-bit words containing magic `0x314c4352`, operation, and payload byte count. Replies use the same magic and length fields with a signed OpenCL status in the operation field. Payloads are limited to 1 MiB. The Windows test worker takes an initial 64-bit inherited reply-mapping handle, with zero selecting pipe-only replies.

| Operation | Request contents | Successful reply |
| --- | --- | --- |
| `OPEN` | NUL-terminated GPU device name | NUL-terminated device name and driver version |
| `BUILD` | Kernel-name byte count, NUL-terminated kernel name, OpenCL C source | Empty |
| `ALLOC` | Buffer slot, size in bytes | Empty |
| `WRITE` | Buffer slot, offset in bytes, data | Empty |
| `MEMORY_ARG` | Argument index, buffer slot | Empty |
| `VALUE_ARG` | Argument index, argument bytes | Empty |
| `RUN` | Global work-item count | 64-bit elapsed GPU nanoseconds |
| `READ` | Buffer slot, offset in bytes, byte count | Requested bytes |
| `RELEASE` | Buffer slot | Empty |
| `RUN_READ` | Global work-item count, buffer slot, offset in bytes, byte count | 64-bit elapsed GPU nanoseconds followed by requested bytes |

All request integers in the table are 32-bit. A session owns one in-order queue, one kernel, and eight buffer slots. The client keeps kernel argument buffers allocated until the commands using them finish, and binds replacement buffers before the next dispatch. `RUN_READ` enqueues a kernel and a blocking read on the same queue. A failed source build can return the driver build log in the reply.

The Windows timing harness includes this single-queue diagnostic protocol. The public Guest ABI uses the multi-object protocol in `abi.h`.

## Regular Android APK

`build_android.ps1` produces the Guest `libOpenCL.so` and the host `libaemuclhost.so` together. The APK starts the host in Android's `:vm` process after preparing the GPU runtime copy. The host opens the system's public OpenCL driver and serves the Guest over the instance's `/dev/socket/aemucl` Unix socket. Peer admission uses the same runner, ancestry and Guest root checks as the Vulkan renderer. Socket connections and their driver objects are retired with the pinned Guest process.

```powershell
$native = 'build/diagnostics/opencl-native/android'
$previous = 'build/diagnostics/rme-geekbench-cached-host'
$stage = 'build/generated/rme-gpu-native'
./native/openclbridge/build_android.ps1 -Ndk $ndk -OpenClHeaders $headers -Output $native
$pack = @("$previous/host-lifecycle-pool", "$previous/guest-pool/guest", "$native/libOpenCL.so",
    $stage, '--native-opencl-host', "$native/libaemuclhost.so")
Get-ChildItem build/generated/rme-gpu-bridge/assets/rme-gpu/licenses -File | ForEach-Object {
    $pack += @('--notice', $_.Name, $_.FullName)
}
python tools/diagnostics/gfxstream-androidpipe-channel/package_gpu_bridge.py @pack
./gradlew.bat --offline :app:assembleRelease "-PrmeGpuBridgeDir=$stage"
```

The staged marker `assets/rme-gpu/native-opencl` activates the paired host. GPU provisioning backs up the original runtime files. Boot with GPU disabled to restore them. Restaging the previous clvk APK reinstalls its own OpenCL asset on the next GPU-enabled boot.

The Unix transport combines each control frame and its small payload in one send. Transfers of at least 64 KiB reuse a sealed-size anonymous mapping created by the host and passed over the authorized connection. The driver takes CPU data directly from that mapping for the duration of a synchronous transfer. Either endpoint can process the stream payload when shared allocation is unavailable. The Guest retains event references locally and attaches one final release to the next request when the host advertises support. The host applies it before dispatching that request. Consecutive releases flush the earlier pending ID, and disconnect reaps the remaining driver objects. `build_android.ps1 -Profile` enables per-operation call, byte and host timing counters in the `RmeOpenCLProfile` log tag, emitted when the connection closes.

## Public ABI verification

Run the Windows build described above, then run `verify_public.sh` from WSL with Linux temp output. `RME_CL_WINDOWS_WORKER` points at the Windows worker, which runs the same host dispatcher against Windows' real vendor driver. The WSL test transports requests over a Unix stream and anonymous pipes. For a WSL-only test, set `RME_CL_HOST_ICD` to a local OpenCL library instead.

```sh
RME_CL_WINDOWS_WORKER=/mnt/e/RedMagicEMU/build/diagnostics/opencl-native/rme-opencl-worker.exe \
  RME_CL_TEST_EVENT_BATCH=1 \
  sh native/openclbridge/verify_public.sh build/diagnostics/opencl-native /tmp/rme-opencl-public
```

The public ABI test covers OpenCL C compilation and link, two queues, native event dependencies and profiling, image sampling and padded transfers, buffer maps with `CL_MEM_USE_HOST_PTR`, and binary reload. With a WSL ICD file in `RME_CL_HOST_ICD`, `RME_CL_TEST_ASYNC_UPLOAD=1` holds pairs of linear buffer, rectangular buffer, and image writes on a native test event until the Guest sends `clFinish`. It then checks the data and padding. `RME_CL_TEST_ASYNC_READ=1` holds reads pending until a Guest synchronization call and checks readback through event wait, status polling, and queue finish. The Windows worker can also run the readback checks against its real vendor driver.

The bridge presents a single-device OpenCL 1.2 subset. Nonblocking linear buffer, rectangular buffer, and image writes retain a private host copy until the native completion callback, even when the caller requests no event. Rectangular buffer and image transfers keep the packed layout across the connection. Each session stages up to 128 MiB of pending uploads. If it cannot reserve storage, nonblocking rectangular buffer and image writes return `CL_OUT_OF_HOST_MEMORY` and a linear buffer write uses its blocking fallback. A previous host executes rectangular buffer and image writes synchronously.

With the asynchronous read capability, a linear buffer read issued with `CL_FALSE` queues native work and returns its status without waiting for readback. Each pending read holds its own host staging buffer and event reference until completion. The host sends ready data before replying to the next Guest operation. The Guest writes the data into the original destination before returning a completed event status, event wait, or queue finish. At most one large completion per response can reuse the transport mapping after the Guest confirms it is mapped. Outstanding readback storage is limited to 128 MiB per session. Pairs with a previous host use the original blocking read path. In profile builds, the normal reply byte counters exclude completion frames and the output timing includes delivery on the operation that drains them.

With the readback slot capability, the socket host also offers a separate memfd with four 16 MiB slots. For asynchronous reads from 64 KiB to 16 MiB, the driver writes into a free slot and the Guest copies from it directly into the application's buffer. A slot stays reserved through the driver event and the ordered completion reply. Reads that exceed the slot size, fill the available slots, or run without the mapping use the private staging path. The host retains the mapping on disconnect until it can establish that the driver has completed its writes.

Device partitioning, user events, native host callbacks, and memory destructor callbacks return `CL_INVALID_OPERATION`.

The device extension string includes `cl_khr_fp16` when the native driver advertises it. The existing scalar query forwards `CL_DEVICE_HALF_FP_CONFIG`. Set `RME_CL_TEST_FP16=1` with a supporting ICD to verify the extension query and a half-precision kernel, or `RME_CL_TEST_FP16=0` with an ICD that lacks it to verify the empty extension list.

## Adreno shared-buffer probe

`shared_buffer_probe.c` runs two synthetic OpenCL kernels on the Android host and verifies every word in a second process. The baseline uses blocking writes and reads. The default candidate maps a regular `CL_MEM_USE_HOST_PTR` buffer backed by memfd. `--qcom-ahb` imports two 4 MiB AHardwareBuffer BLOBs via `cl_qcom_android_ahardwarebuffer_host_ptr`. The CPU process receives the data DMA-BUF descriptors over a Unix socket and brackets CPU access with `DMA_BUF_IOCTL_SYNC`. The GPU process retains the AHardwareBuffers until it releases the OpenCL objects. The probe requires a cached Android system heap for this import.

```powershell
$clang = "$ndk/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
$probe = "$out/shared-buffer-probe"
& $clang --target=aarch64-linux-android29 -std=c11 -O2 -Wall -Wextra -Werror `
    '-Wl,-z,max-page-size=16384' -I $headers native/openclbridge/shared_buffer_probe.c `
    native/openclbridge/abi_io.c -ldl -landroid -o $probe
```

Run the executable from an Android ARM64 host's `/data/local/tmp` and pass `/vendor/lib64/libOpenCL.so` as the driver. Add `--qcom-ahb` for the AHardwareBuffer comparison or `--expect-mismatch` to check that changing one expected word exits with status 1. Each comparison interleaves 24 timed samples per path after four warmups. `transfer` includes explicit transfers and CPU cache synchronization. `device` comes from the kernel event. `ready` includes driver submission and the wait for GPU output, plus transfer and cache synchronization. Allocation, compilation, CPU initialization, and CPU verification are outside these intervals.

For the N64 comparison, stage the same probe executable in a temporary directory inside the NX809J runtime's `/data/local/tmp`. Pass `--qcom-ahb --guest-runner HOST_GUEST_ROOT HOST_N64_RUNNER GUEST_PROBE_PATH`. Run as the instance owner. The unmodified runner starts the CPU consumer with a private inherited Unix stream. The host passes both the ordinary memfd and the AHardwareBuffer DMA-BUFs over that stream. A successful run prints `N64_GUEST_ADRENO_SHARED_BUFFER_ROUNDTRIP_OK`. Compile both copies with `-DRME_CL_PROBE_WIDTH=256` or `512` to try 256 KiB or 1 MiB per buffer. Remove the two temporary copies after testing.

The AHardwareBuffer path uses the allocation type and handle argument shown in [MNN's OpenCL headers and backend](https://github.com/alibaba/MNN/blob/master/3rd_party/OpenCLHeaders/CL/cl_ext.h#L387-L401). The [Qualcomm OpenCL guide](https://docs.qualcomm.com/doc/80-NB295-11/80-NB295-11_REV_C_Qualcomm_Snapdragon_Mobile_Platform_Opencl_General_Programming_and_Optimization.pdf) describes the vendor extension in section 7.4.2.3.
