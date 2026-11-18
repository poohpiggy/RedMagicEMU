# gfxstream GetInstanceProcAddr with a null instance

This research patch targets locally examined gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`. The local source contains prior
TRACE diagnostics and is not included here. Preserve all upstream copyright
notices and Apache-2.0 and other applicable licenses when using it.

The actual `gfxstream_vk_GetInstanceProcAddr` body forms `&instance->vk` even
when the Vulkan caller passes the valid null-instance value for global queries.
Mesa's target `vk_instance_get_proc_addr` explicitly accepts a null instance.
An extracted-real-function test with a fake dispatcher and UBSan reports null
member access on the original and passes with this patch. The handle's first
member has offset zero, so this is not evidence that the uninstrumented code
actually crashes on a device or caused the reported black screen.

```sh
python3 tools/diagnostics/gfxstream-d7a-null-instance/run.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --patch tools/diagnostics/gfxstream-d7a-null-instance/fix.patch \
  --cxx /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++
```

Omit `--patch` to reproduce the UBSan failure. This test uses the available
system `libubsan`, a local NDK host compiler and no GPU. Android ARM64 object
compilation can be checked using `compile-android.sh` with the same four input
arguments documented by `../gfxstream-null-host-connection/README.md`. An
independent build produced AArch64 REL SHA-256
`48ed5454e618cec7dc1d22082f14e60ac512aadbc785be077e25b3e520a85add`.

The patch is not integrated into EMU and is not a linked client. Loading it
into Guest apps, command transport, real resources, host decoding and GPU
rendering are still unresolved. No phone, Guest file, imported package or
device node is accessed by this test.
