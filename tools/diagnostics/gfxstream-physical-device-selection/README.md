# d7a Vulkan physical device selection

This research patch targets gfxstream `d7a33801fb557cebd4f8564a8af431669b3f471d`.
It is not included in RedMagicEMU or an APK. It selects only a driver-reported
integrated or discrete GPU with standard Vulkan variant 0, Vulkan 1.2 or newer,
and a nonempty queue family supporting both graphics and compute. It uses that same family for device
creation, queue retrieval and the backend's queue-family index. Existing
preference scores apply among eligible devices; when external-memory
capabilities are unavailable, the first **eligible** device is selected.

The backend currently creates one graphics queue. A device with graphics and
compute only in different families is therefore rejected; this patch does not
implement separate queues. A reported GPU type alone cannot prove that a
driver executes on physical hardware. This patch does not establish Guest
Vulkan or Geekbench functionality, handle initialization failure cleanup, or
contain the separate device-creation-result patch.

The regression calls the actual initialization selection code in a copied
Linux host build. It uses a real llvmpipe CPU handle for backing queries and
test-only synthetic device properties and queue-family callbacks. Each case
stops at the `VkDeviceQueueCreateInfo`, before device creation and resource
allocation. Synthetic candidates are not real GPUs; their properties test
control flow only. Cases cover CPU and virtual rejection, eligible GPU behind
an ineligible device with and without external-memory capabilities, Vulkan
1.1 rejection, no compute, split families, a combined second family, zero
queue count, the existing preference between two eligible devices, and a
nonzero API variant that must not pass the numeric version check.

Run with the previously built fixed source, Linux host build and dependencies:

```sh
python3 tools/diagnostics/gfxstream-physical-device-selection/run.py \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/build-host \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/sysroot/usr/bin/ninja \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/prefix/lib:/var/tmp/rme-gfxstream-build.hTnuwsWV/sysroot/usr/lib/x86_64-linux-gnu \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/sysroot/usr/include
```

The script verifies the source file SHA-256, forces the llvmpipe ICD, verifies
the backing device is reported as a CPU, applies `fix.patch` with zero fuzz,
and runs each scenario in a fresh process. It downloads nothing. The copied
build retains dependencies on its original toolchain and libraries. The
actual `vkCreateDevice` and `vkGetDeviceQueue` calls are not exercised. The
test does not operate on an Android device or execute a physical GPU workload.
