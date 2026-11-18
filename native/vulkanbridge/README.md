# Vulkan bridge handshake

The handshake in this directory is standalone; it is not a Vulkan driver or a
running EMU service. It is not connected to EMU startup and cannot enumerate
or execute on a GPU.

Each frame has three little-endian 32-bit fields: magic `RVK1`, message type,
and payload length. A `HELLO` message (type 1) has one nonzero protocol version;
the `HELLO_REPLY` message (type 2) has the server version and a 0/1 status.
Lengths are exact and fixed. Matching versions yield success; rejection, bad
frames, EOF, transport failure, or expiration are reported separately.

The caller supplies an already-connected stream socket and must hold it
exclusively for the entire call. It owns the descriptor, retains its flags, and
must close it after rejection or failure. One monotonic deadline covers the
entire handshake. A successful handshake is not authentication, evidence of
GPU availability, or a guarantee that the connection remains usable.

Run the local socketpair regression under WSL or Linux:

```sh
sh native/vulkanbridge/tests/run.sh
```

This tests framing, version refusal, fragmented I/O, malformed messages,
timeouts and closed peers. Tests do not establish Android device behavior.
The next integration must independently enforce connection state (including
rejecting repeated handshakes), instance and client isolation, authorization,
resource limits, transport lifecycle, and actual backend capability checks.
The current functions neither grant Guest access nor solve those requirements.

## Host Android surface helper

`host_surface.c` can create and destroy a surface for an EMU-owned native
window using the Guest device's actual host Vulkan instance. It is not yet
connected to the renderer, client protocol or an EMU window. Its caller must
verify instance ownership and enabled extensions. Local fake-entrypoint tests
and Android ARM64 object-build instructions are in
[`gfxstream-host-android-surface`](../../tools/diagnostics/gfxstream-host-android-surface/README.md).
This helper has not displayed a Guest frame.

## Physical-device selector

`gpu_select.c` accepts an existing Vulkan instance and that instance's
`vkGetInstanceProcAddr`. It enumerates physical devices and their queue families,
and selects the first device whose **driver-reported** type is integrated or
discrete GPU, API version is Vulkan 1.2 or newer, and which has a nonempty
compute queue. For that first eligible device, it prefers a nonempty queue
family supporting both graphics and compute. If there is none, it retains the
first nonempty compute queue; this fallback does not imply graphics support.
It does not search later devices solely for a combined family, create a second
queue, or advertise graphics capability. CPU, virtual, other and unknown
device types are excluded. The
component never creates a Vulkan instance or logical device, and does not run
commands. The caller owns the instance and must keep it alive while using the
returned physical-device handle and must prevent concurrent destruction during
selection. The caller must create an instance requesting at least Vulkan 1.2.

Driver-reported type is not proof of hardware identity or a driver security
check. Existing backend checks remain the caller's responsibility. This
selector does not implement a Guest Vulkan driver or establish GPU performance.
It returns distinct statuses for missing functions, enumeration errors, limits,
invalid responses and no matching device; the original `VkResult` is retained
for enumeration. Device and queue counts are bounded at 256, and
`VK_INCOMPLETE` is retried at most three times. The Vulkan property and queue
queries return `void`, so the selector does not invent error codes for them. A
second queue-family count query checks that the observed count stayed the same
across enumeration; a discrepancy is rejected as an invalid response.

Run the local fake-loader tests, real llvmpipe CPU rejection, and Android ARM64
probe build without contacting a device:

```sh
sh /mnt/e/RedMagicEMU/native/vulkanbridge/tests/run_gpu_select.sh
```

Set `ANDROID_NDK_LINUX` and `RME_CPU_VULKAN_ICD` if the existing NDK r28c or
llvmpipe ICD are elsewhere. The script prints its fresh `/var/tmp` artifact
directory and leaves `gpu-select-android-arm64` there for a separately
authorized device test. That program creates a Vulkan 1.2 instance and prints
the reported devices and selected queue; it exits nonzero if no GPU matches or
logical-device creation or cleanup fails.
The fake-loader tests use fake devices and are not evidence of a real GPU. The
llvmpipe test must report its CPU device and then reject it.

## Logical-device lifecycle

`gpu_device.c` calls the selector and, only for a selected GPU, creates a Vulkan
logical device with one queue from the selected compute family. It resolves all
required Vulkan entrypoints before creation, preserves the real `VkResult` from
`vkCreateDevice`, and destroys the logical device immediately if queue retrieval
returns a null handle. Callers zero-initialize `rme_vk_gpu_device` before its
first open and serialize use. The state exclusively owns its device: it must
not be copied or manually zeroed while active. A second open while active is
refused. Close destroys the device once; repeated close is a no-op.

The caller owns the instance throughout the device lifetime and must finish all
submitted work and destroy any resources it creates before calling close. This
component neither submits work nor waits for externally submitted work. It is
not an EMU Guest bridge, a driver authentication mechanism, or proof of hardware
acceleration. Its fake tests use mock Vulkan functions and include missing
entrypoints, creation failures, null handles, CPU rejection and cleanup checks.
The ARM64 program described above also creates and destroys the logical device;
it also creates, binds and destroys a 64 KiB storage/transfer buffer, and
reports success only after those steps succeed.

`rme_vk_gpu_open_with_features` additionally accepts an explicit Vulkan 1.0
`VkPhysicalDeviceFeatures` request. It queries the selected physical device,
rejects a requested unsupported member before `vkCreateDevice`, and passes
only the requested members to device creation. Nonzero request values are
normalized to `VK_TRUE`. After success, `rme_vk_gpu_enabled_features` reports
the actual requested set; it returns NULL before successful open and after
close. The original `rme_vk_gpu_open` and an explicit NULL request keep the
previous behavior: no feature query and `pEnabledFeatures == NULL`.
Only the first device selected by the existing GPU policy is checked; a missing
feature does not trigger a search for another GPU.

This is core Vulkan 1.0 only. It does not enable extensions or Vulkan 1.1/1.2
`pNext` features, check feature dependencies, implement per-Guest device
isolation, or validate shaders. The caller must supply a Vulkan-valid
combination. Physical support or enabled features do not authorize submitting
untrusted shaders to the existing compute component.

## Buffer lifecycle

`gpu_buffer.c` uses a previously opened GPU device to create a buffer of a
caller-selected size and any nonempty combination of storage and transfer
usage. It obtains device commands through that device's `vkGetDeviceProcAddr`,
queries actual memory requirements and memory types, honors required dedicated
allocation, and binds the allocation. It rejects invalid or over-budget sizes,
incompatible types, and memory types requiring unsupported protected, lazy or
AMD device-coherent behavior. Its budget is a **per-call byte ceiling**, not a
global heap budget.

The buffer state owns its Vulkan buffer and memory exclusively. Initialize it
to zero, do not copy or clear it while active, and keep its parent GPU device
and instance alive until buffer destruction. The parent device tracks allocations
made through this component and refuses closure while any remain. The caller
must serialize access and account for **all** other device memory allocations
against `maxMemoryAllocationCount`; this component cannot observe allocations
made elsewhere. The caller must ensure no externally submitted work uses the buffer before
destruction. Buffer destruction is also refused while a tracked submission is
active. The component performs no CPU mapping, data transfer, queue
submission, or synchronization and is not a Guest Vulkan bridge.

The current logical-device creation enables no extension or feature chain; in
particular it does not enable `maintenance4`. The per-call byte ceiling is not
a query of the device's `maxBufferSize`. If maintenance4 is enabled in a future
version, query `VkPhysicalDeviceMaintenance4Properties::maxBufferSize` and
validate the requested buffer size before creation.

## Queue submission and fence lifecycle

`gpu_submission.c` submits one caller-recorded command buffer or a genuine
empty submission to the selected queue, with a newly created fence. The caller
must externally synchronize the queue, keep the command buffer, its pool and
referenced resources alive, and ensure the command buffer is executable and
belongs to this device. Initialize each submission state to zero and never copy
or clear an active state. The instance and GPU device must outlive it.

`VK_TIMEOUT` and host/device memory exhaustion during a fence wait retain the
pending fence and may be retried. They do not indicate completion. A wait that
returns `VK_ERROR_DEVICE_LOST` means execution failed and the device is no
longer available for new work, but the fence covered by that wait can be
disposed for lifecycle purposes. Other pending submissions must be waited
separately. A repeated wait on this state still reports failure; disposing the
fence is cleanup, not computational success. See the Vulkan specification's
[Lost Device section](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#devsandqueues-lost-device).

Until disposal, device close and destruction of buffers owned by this component
are refused. Any submit failure, or any unexpected wait failure, preserves the
actual `VkResult`, retains the fence and marks the device unavailable for
further submissions or buffer creation. This conservative policy has no
recovery for an unknown outcome, and the caller must retain all potentially
referenced resources. A later device-loss result upgrades the device's recorded
failure; each submission retains its own result. Other resources and externally
submitted work are the caller's responsibility. The defensive fake
`vkCreateFence` DEVICE_LOST case exercises a nonconforming return; it is not a
listed Vulkan return for that function.

The ARM64 probe also submits an empty batch and waits for its fence. It reports
`VULKAN_EMPTY_SUBMISSION_OK` only after successful wait and fence disposal;
failure exits nonzero; known lost completion is cleaned and unresolved resources
are retained. This does not
execute a shader, establish Guest support, or prove GPU performance.

## Single-buffer compute

`gpu_compute.c` builds a descriptor, pipeline and command buffer using a
caller-supplied SPIR-V module, entry point, storage buffer and dispatch groups,
then submits it through the existing submission component. This covers one
buffer at set 0 binding 0, with no push constants, specialization, additional
descriptors or GPU-to-GPU dependency management. It is not a general Vulkan
implementation or a Guest interface. Size and group limits, descriptor limits,
buffer usage, SPIR-V word count/alignment/magic and entry length are checked;
the shader is limited to 16 MiB and the entry name to 255 bytes.

The shader must be trusted and already validated for the target device and its
enabled features/extensions. The caller must ensure its entry point, resource
interface, local workgroup size and invocation count comply with device limits,
and all memory accesses stay within the buffer. The component does not parse or
validate SPIR-V instructions. It serializes none of the queue or buffer access:
the caller must do so. Its two barriers cover host writes before the dispatch
and shader writes before host reads only. The caller handles mapped-memory
flush/invalidation and all synchronization involving other GPU operations or
queues. Only one such compute operation may reference a buffer at a time, and
start refuses an already active tracked submission.

The operation holds its buffer until `rme_vk_compute_dispose` succeeds, and the
buffer's device stays alive as long as the buffer does. Timeout and unknown
execution cannot dispose any potentially in-use resources. Device loss reports
failure even where cleanup is permitted. Callers must zero-initialize and never
copy or manually clear active state. The fake lifecycle tests do not execute a
shader; the separate `tools/diagnostics/vulkanbridge-compute` program tests the
component with a real host Vulkan loader when run on an authorized GPU.
