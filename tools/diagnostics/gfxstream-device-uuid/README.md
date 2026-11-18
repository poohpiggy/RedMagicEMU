# Android Vulkan device identity selection experiment

The fixed d7a selection already checks device type, Vulkan version and a
graphics+compute queue. This isolated patch additionally requires an external
provider's approved device UUID and driver UUID, and rejects missing, zero,
ambiguous or unmatched identities before the existing eligibility selection.
The device matched here is the actual `VkPhysicalDevice` chosen by that selection.
No GPU policy or blacklist is removed or replaced.

`getApprovedVulkanIdentity` is deliberately only declared. A real provider must
populate it from a physical device checked under the existing policy, and its
initialization order and lifetime must be integrated. No such provider is
implemented here; the Android backend remains unable to link until it exists.
This experiment is not Guest Vulkan support or a safety approval for any GPU.

Run `verify.py <merged-VkCommonOperations.cpp> <android-build-dir> <host-cxx>
<isolated-output-dir>`. It first reproduces incorrect identity selection in
the unmodified actual selection block, then tests the modified block with
synthetic metadata only. It also cross-compiles the complete modified source
using the existing Android build command and checks the required unresolved
provider symbol. The script does not access a Vulkan loader, GPU or phone.

For the existing offline inputs:

```sh
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-device-uuid/verify.py \
  /var/tmp/rme-d7a-vk-merged-android.IFuEeE2h/source/host/vulkan/VkCommonOperations.cpp \
  /var/tmp/rme-d7a-vk-merged-android.IFuEeE2h/build \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/bin/cxx \
  /var/tmp/rme-pipe-channel.w3wvezNf
```

The last directory must already exist. The resulting object is
`/var/tmp/rme-pipe-channel.w3wvezNf/VkCommonOperations-identity-android.o`.
