# Host Vulkan bridge component compute test

This standalone Android-host program uses the existing Vulkan selector, device,
buffer, submission and single-buffer compute components, plus
`../vulkan-compute/compute.comp` and its
output checker. It does not run inside or connect to an EMU Guest. It accepts
only a Vulkan 1.2 compute device whose driver reports a discrete or integrated
GPU; the reported type is not independent proof of hardware identity.

The test allocates its own host-visible storage buffer, writes 64 input values,
flushes noncoherent memory where necessary, executes the shader, waits for the
fence, invalidates noncoherent memory where necessary, and checks every word
against `3 * index + 7`. A successful execution and cleanup prints
`VULKAN_BRIDGE_COMPUTE_OK words=64` and exits 0. Run the same binary with
`--expect-mismatch` as a negative control: it deliberately changes one expected
value, prints `OUTPUT_MISMATCH ... (intentional expectation mismatch)` and
exits 1. That negative result must only be considered valid when it reaches the
output check; an earlier device or driver failure is not a successful control.

Submission failure or an unresolved wait returns nonzero and leaves potentially
in-use Vulkan objects untouched. A fence wait reporting device loss is a
failure; it permits cleanup only for the completed lifecycle. The diagnostic
uses a 10-second fence timeout, which cannot guard against a driver call that
itself hangs. A successful isolated compute test does not establish Guest
Vulkan, rendering, OpenCL, Geekbench compatibility, or general conformance.

Build offline under WSL, without connecting to a device:

```sh
sh /mnt/e/RedMagicEMU/tools/diagnostics/vulkanbridge-compute/build.sh
```

The script validates the existing shader as Vulkan 1.2 SPIR-V, checks the
output checker against 64 good words, a deliberately wrong expectation and each
individual wrong word, verifies that a real llvmpipe CPU ICD is rejected, and
cross-compiles an Android ARM64/API 29 executable with 16 KiB ELF alignment.
It prints a unique `/var/tmp` artifact path and SHA-256. The host checker test
does not run the shader, and the CPU ICD is rejected before compute. The normal
and intentional-mismatch runs both require separate execution on the authorized
Android host to verify real GPU behavior. The program does not modify the Guest.
