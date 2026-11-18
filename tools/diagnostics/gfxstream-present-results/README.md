# Experimental gfxstream present paths

`--single-only` is a separate, narrower experiment. Its generated guest
encoder accepts only `swapchainCount == 1` with `pResults == NULL`, then
uses the original request and response wire unchanged. The host decoder's
present branch is unchanged. Other calls are rejected before encoding; an
output array, if provided, is filled with the error. **This is not conformant
Vulkan WSI:** the explicit `VK_ERROR_FEATURE_NOT_PRESENT` rejection is not
among the return codes for this command in the d7a Vulkan registry. Keep WSI
extensions hidden and do not install or expose this experiment. It is a proof
of the single-chain wire path, not a completed implementation or a rendering
result.

The fixed d7a generator does not generate the guest swapchain encoder. Its
host `vkQueuePresentKHR` decoder writes only the aggregate `VkResult`; the
per-swapchain `pResults` are lost. `generate.py` generates an isolated guest
encoder and, with `--candidate`, a paired experimental decoder/encoder that
transfer `pResults` before the aggregate result. The encoder supplies a
zero-initialized temporary array when encoding the request so it does not read
the application's output array before the driver writes it. This is a source
generation experiment, not an integrated Vulkan implementation.

**Do not connect or package these outputs.** The `--candidate` pair reuses the upstream opcode
with a different response frame. Both peers must agree on an explicit protocol
version before either can use the change; no such negotiation is currently
connected to the gfxstream pipe. The separately implemented `vkb_*_hello`
functions are not connected to that pipe. A one-sided RenderControl extension
advertisement is not sufficient to prove mutual protocol agreement.

The following uses local, previously acquired source/build inputs and writes
to new temporary output directories. No GPU, device, Guest or host system
modifications are involved. The source copy includes existing TRACE
instrumentation; it is not claimed to be a pristine upstream checkout.

```sh
SOURCE=/var/tmp/rme-gfxstream-build.hTnuwsWV/src
NDK=/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
python3 tools/diagnostics/gfxstream-present-results/generate.py "$SOURCE" /var/tmp/rme-present-base-new --clang-bin "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
python3 tools/diagnostics/gfxstream-present-results/generate.py "$SOURCE" /var/tmp/rme-present-pair-new --clang-bin "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin" --candidate
python3 tools/diagnostics/gfxstream-present-results/generate.py "$SOURCE" /var/tmp/rme-present-single-new --clang-bin "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin" --single-only
python3 tools/diagnostics/gfxstream-present-results/test.py /var/tmp/rme-present-base-new  # expected failure: no response write
python3 tools/diagnostics/gfxstream-present-results/test.py /var/tmp/rme-present-pair-new  # requires a host C++17 toolchain
python3 tools/diagnostics/gfxstream-present-results/test_single.py /var/tmp/rme-present-base-new /var/tmp/rme-present-single-new
python3 tools/diagnostics/gfxstream-present-results/compile_guest.py /var/tmp/rme-present-pair-new /var/tmp/rme-encoder-native-validation-v1 "$SOURCE" /var/tmp/rme-present-guest-new
python3 tools/diagnostics/gfxstream-present-results/compile_host.py /var/tmp/rme-present-pair-new /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json /var/tmp/rme-present-host-new
python3 tools/diagnostics/gfxstream-present-results/compile_guest.py /var/tmp/rme-present-single-new /var/tmp/rme-encoder-native-validation-v1 "$SOURCE" /var/tmp/rme-present-single-guest-new
python3 tools/diagnostics/gfxstream-present-results/compile_host.py /var/tmp/rme-present-single-new /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json /var/tmp/rme-present-single-host-new
```

The paired CPU test extracts response statements from generated source and exercises
two results, the aggregate result, a null results array, and a trailing sentinel
with an in-memory stream. It does not execute the complete decoder, its packet
parser, the actual renderer, or a real Vulkan present. The Android test builds
seven generated guest objects and one generated host decoder object; it does
not link a backend or prove interoperability. The single-path test checks the
actual generated guard, compares the remainder of that guest function and
the host decoder branch with the baseline, and exercises rejection without
performing a real queue operation. Actual swapchain images remain
unavailable: the current host decoder returns raw host image values without
registering boxed identities or the image metadata needed for image views.
Snapshots and multi-window compositing are also not implemented by this work.
