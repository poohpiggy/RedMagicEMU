# gfxstream d7a Mesa object dependencies: ARM64 build study

This computer-only study compiles the fixed local gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`'s actual Mesa `vk_object.c`,
`sparse_array.c`, `hash_table.c`, `ralloc.c`, `u_printf.c` and the previously
generated `vk_enum_to_str.c`. It combines them with the previously compiled
encoder/base/MemStream relocatable and compares strict Android linker errors.
It neither executes the compiled code nor communicates with a Guest or GPU.
The upstream sources are not distributed here; preserve their existing Intel
MIT, AOSP and other applicable copyright notices and licenses on redistribution.

Run with pre-existing local source, generated files, AOSP and NDK inputs:

```sh
python3 tools/diagnostics/gfxstream-d7a-vk-object/build.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --generated /var/tmp/rme-gfxstream-build.hTnuwsWV/build-guest \
  --aosp /var/tmp/rme-mesa-android-6ekppv9b \
  --ndk /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c \
  --encoder-dir /var/tmp/rme-vk-encoder-unified-6uu4ltgk \
  --memstream-dir /var/tmp/rme-vk-memstream-vft0qxdn
```

The script verifies the baseline object and each source hash, the relevant
Mesa runtime source listing, the original missing symbols and the chosen
hardware-buffer headers. It first runs an actual Android ARM64/API 35 shared
link using NDK `libc++_shared`, `liblog` and `libandroid` linker interfaces and
`--no-undefined`, then compiles six AArch64 relocatable objects, groups them
with `ld.lld -r` and runs the same strict link again. Command lines, compiler
logs, actual header dependencies and hashes, both linker logs, and exact
symbol-set differences are kept in a fresh `/var/tmp/rme-vk-object-*` directory.
The script downloads nothing and refuses to treat a failed link as success.

Two independent runs produced identical object and observed-input hashes and
the same combined **relocatable**, not shared, object SHA-256
`71c0653cf47a41f71d38b48dfe120b725e3bf73514a55df89b4a964d9cbab663`.
The `llvm-nm` undefined set changed 131 -> 139: two symbols resolved and
ten new names appeared. The strict Android linker errors changed 23 -> 21:
`vk_object_free` and `vk_object_zalloc` resolved without adding a new strict
error. No shared library was generated. Both runs retain the unresolved
`vk_sync_dummy_type`. Upstream documents that dummy sync for WSI temporary
objects expects host-side synchronization; that behavior and the host wait
path have not been validated here, so this study neither links nor executes
the dummy implementation or claims working synchronization.

This is a hand-built diagnostic, not an Android Soong build. The inputs include
existing locally generated headers, a locally generated enum source and earlier
diagnostic changes. The C compilation uses the existing encoder's platform
configuration without `HAVE_POSIX_MEMALIGN`, selecting Mesa's built-in aligned
allocation fallback; this does not establish Android 16 ABI or behavior. The
baseline still depends on two Mesa hardware compatibility headers. The NDK
platform libraries are link interfaces, not evidence of availability in a
target device's linker namespace. Source, toolchain and generated inputs are
not included here, so the experiment is not a self-contained release build.
Real instance isolation, resource ownership, input validation, transport,
host Vulkan decoding, application entrypoints and the remaining link errors
are unresolved. Nothing here establishes Guest Vulkan or GPU operation.
