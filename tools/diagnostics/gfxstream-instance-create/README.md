# gfxstream Vulkan instance creation input and cleanup

This diagnostic patch targets gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`, using the locally inspected
`guest/vulkan/gfxstream_vk_device.cpp` with SHA-256
`6b53405174b89935f32086e6529d2e6cf65fe7666e18ecbcf50e522d195ec045`.
It preserves the caller's `const VkInstanceCreateInfo` by passing a shallow copy
with the filtered extension list to the encoder. It frees the guest allocation
on setup, connection, and encoder failure, and destroys the host instance if
the subsequent Mesa instance initialization fails. The source tree is not
changed; the patch is applied to a temporary copy. Preserve upstream copyright
and licenses when using its source.

`run.py` extracts the complete actual instance creation function, compiles it
with fake connection, encoder, allocation and Mesa dependencies, and runs each
case in a separate process. This intentionally tests function behavior in
isolation; it does not test transport, real Mesa or GPU rendering. Its fake
`VK_HOST_CONNECTION` checks a missing connection before dereference, so the
unpatched test isolates input and cleanup failures rather than reproducing that
separate upstream macro bug.

On the examined source without the patch, setup leaks the local instance;
connection, encoder and creation failures change the caller's input; failed
Mesa initialization leaves the host instance alive; read-only input segfaults.
With the patch, setup, connection, encoder, creation, initialization, success
and read-only cases pass. To reproduce (Linux, host clang, no C++ runtime needed):

```sh
python3 tools/diagnostics/gfxstream-instance-create/run.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --patch tools/diagnostics/gfxstream-instance-create/fix.patch \
  --cxx /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ \
  --compiler-arg=-nostdlib++
```

Omit `--patch` to reproduce the expected failures. The unmodified full source
with this patch also compiled into an Android ARM64 object using:

```sh
sh tools/diagnostics/gfxstream-instance-create/compile-android.sh \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/build-guest \
  /var/tmp/rme-mesa-android-6ekppv9b \
  /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
```

The script rejects Mesa Android stub headers. An object build is not a linked
client, and neither the patch nor these tests demonstrate Guest Vulkan.
