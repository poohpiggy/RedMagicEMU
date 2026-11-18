# gfxstream fence FD export without a sync backend

This is a research-only patch for the locally examined gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`. The local
`guest/vulkan_enc/ResourceTracker.cpp` contains prior diagnostic modifications
and has SHA-256 `968ac1488342429007ae6dc797c241b237399ed18ddf0988b43ff789f7818990`.
The upstream source is not included here. Preserve its existing AOSP copyright,
Apache-2.0 license and all other applicable notices if redistributing a patch or
source.

In `on_vkGetFenceFdKHR`, when Goldfish is compiled out and native Virtio sync is
unavailable, the original code writes no FD but clears the stored `info.syncFd`
and returns `VK_SUCCESS`. The patch returns `VK_ERROR_UNKNOWN` before either
operation; the caller's output and the recorded FD remain unchanged. The
[Vulkan specification's return-code rules](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html)
allow `VK_ERROR_UNKNOWN` for an unexpected implementation error. Returning
`VK_SUCCESS` with `-1` cannot represent a missing backend: a sync FD of `-1`
can instead denote an already signaled fence. This change does not claim the
device is lost or that a memory allocation failed.

`run.py` extracts the actual complete method from the supplied source into a
temporary build and exercises it with in-process C++ substitutes. The runner
checks the same-source Vulkan header's SYNC_FD flag, then compiles both Goldfish
macro configurations. Substitute handles are integers; the fake Goldfish and
native helpers do not open, close or duplicate any real fence FD or access an
Android device. The compiler and harness still use normal temporary files and
standard input/output.

```sh
python3 tools/diagnostics/gfxstream-d7a-fence-fd/run.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --patch tools/diagnostics/gfxstream-d7a-fence-fd/fix.patch \
  --cxx /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ \
  --compiler-arg=-nostdlib++
```

Without `--patch`, the Goldfish-disabled cases for fence status `VK_SUCCESS` and
`VK_NOT_READY` each incorrectly report success and clear the stored FD. With
the patch, both macro variants pass those cases plus native success/failure,
device loss, missing fence, wrong handle type, missing fence record, missing
external flag and missing export flag. The Goldfish-enabled substitute also
models queue failure; the method reports an error without changing the caller's
output or relinquishing the recorded fence state. No real ioctl or FD is tested.

To cross-compile the patched full `ResourceTracker.cpp` as Android ARM64/API 35
objects for both macro values without linking or executing them:

```sh
sh tools/diagnostics/gfxstream-d7a-fence-fd/compile-android.sh \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/build-guest \
  /var/tmp/rme-mesa-android-6ekppv9b \
  /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c \
  /var/tmp/rme-vk-encoder-unified-6uu4ltgk
```

For the earlier macro-0-only revision, two builds matched: macro 0 SHA-256
`ce64af9af4d9c2186b4ee7d2286d3b38c2a470eb83d39666cac8a8882a4f1128`,
macro 1 SHA-256
`1967e2debb661409e64f8bf8fd2d4545c6542cfd071583f52253e5f6840871e8`.
Those historical hashes do not describe the current patch. Both were AArch64
REL objects; the build requires prior local generated headers,
NDK/AOSP inputs and Mesa compatibility headers. It is not a Soong build or an
Android 16 ABI check.

The diagnostic client build applies this patch by default; a full Android ARM64
client was strictly linked offline. This does not fix QSRI or any other sync
path. No Guest, GPU, actual fence FD, or `/dev/goldfish_sync` device was
accessed, and the result has not been run on Android.
