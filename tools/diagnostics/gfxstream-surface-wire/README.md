# Isolated paired Vulkan surface query codecs

The pinned d7a generator limits `VK_KHR_surface` to host dispatch, so the
guest encoder and host decoder do not contain the surface support,
capabilities, formats, present-modes or destroy commands. `generate.py`
builds both sides in a new temporary output using the same Vulkan registry.
It does not generate or expose `vkCreateAndroidSurfaceKHR`, transfer a Guest
native-window pointer, or enable any Vulkan extension.

The generated queries originally read caller output fields when forming
requests. For support, capabilities, formats and present modes, the isolated
generator now writes zero-initialized output placeholders while retaining the
normal response destinations. Count-only enumeration sends a zero input count;
enumeration with an output array preserves the requested capacity. The
generated Guest destroy command already sends a null allocator, and the host
decoder now also discards any received allocator and calls the driver with
null, matching the EMU host surface's null-allocator creation.

Tests execute statements extracted from the generated functions with sentinel
data: all six cases for the previously generated output fail before these
changes and pass afterward. Additional tests cover both count-only and
non-empty-array capacities. This does not run the complete generated decoder
or validate arbitrary input, caller authorization, or the Android driver.

An Android ARM64 decoder object and seven matching encoder source objects
compile separately. The host compile copies the existing source and replaces
the generated headers together, avoiding mixed definitions; it does not link
the complete decoder, execute the packet parser, or contact a driver. The
default output has no surface creation wire, shared client/server negotiation,
per-instance surface/swapchain ownership, Android lifecycle teardown, or
visible frame. These files are an isolated experiment, not a working WSI.

For the local pinned inputs, generate into a previously nonexistent directory:

```sh
SOURCE=/var/tmp/rme-gfxstream-build.hTnuwsWV/src
NDK=/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
OUT=/var/tmp/a-new-surface-wire-directory
python3 tools/diagnostics/gfxstream-surface-wire/generate.py "$SOURCE" "$OUT" \
  --clang-bin "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
python3 tools/diagnostics/gfxstream-surface-wire/check.py "$OUT/guest/VkEncoder.cpp" "$OUT/host/VkDecoder.cpp"
CXX="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" \
  python3 tools/diagnostics/gfxstream-surface-wire/test_support_output.py "$OUT/guest/VkEncoder.cpp"
CXX="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" \
  python3 tools/diagnostics/gfxstream-surface-wire/test_outputs.py "$OUT/guest/VkEncoder.cpp" "$OUT/host/VkDecoder.cpp"
python3 tools/diagnostics/gfxstream-surface-wire/compile_host.py "$OUT" \
  /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json \
  /var/tmp/a-new-host-object-directory
python3 tools/diagnostics/gfxstream-present-results/compile_guest.py "$OUT" \
  /var/tmp/rme-encoder-native-validation-v1 "$SOURCE" /var/tmp/a-new-guest-object-directory
```

The local upstream source contains earlier diagnostic instrumentation, and the
build relies on local generated headers, Android NDK and AOSP inputs. No
source, firmware, hardware, real GPU or Guest image was changed or run.

`apply_fixes.py OUTPUT` is also callable independently after generating the
same paired surface commands together with other Vulkan command families. It
checks the expected code layout and fails rather than silently applying an
incompatible edit.

With `--private-create`, the isolated generator additionally creates the paired
EMU-only command `vkCreateEmuSurfaceRME(VkInstance, VkSurfaceKHR*)`; the guest
request carries no native-window pointer. The host adapter acquires the EMU
process's window, creates a surface using the supplied host instance and its
Vulkan dispatch, and tracks that surface for matching-instance destruction.
The generated response maps the host handle to a boxed guest identity. This
private opcode has no connected protocol negotiation and is not exposed to
applications; it is not a replacement for Android surface lifecycle handling.

The following local-only commands use a fresh directory (the generator requires
that `GEN` not exist; the adapter test requires `WORK` to exist):

```sh
WORK=$(mktemp -d /var/tmp/rme-surface-create.XXXXXXXX)
GEN=$WORK/generated
python3 tools/diagnostics/gfxstream-surface-wire/generate.py "$SOURCE" "$GEN" --clang-bin "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin" --private-create
python3 tools/diagnostics/gfxstream-surface-wire/check_create.py "$GEN/guest/VkEncoder.cpp" "$GEN/host/VkDecoder.cpp"
python3 tools/diagnostics/gfxstream-surface-wire/test_create_wire.py "$GEN" /var/tmp/rme-gfxstream-build.hTnuwsWV "$NDK"
sh tools/diagnostics/gfxstream-surface-wire/test_host_guest_surface.sh /var/tmp/rme-gfxstream-build.hTnuwsWV "$NDK" "$WORK"
CXX="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" python3 tools/diagnostics/gfxstream-surface-wire/test_outputs.py "$GEN/guest/VkEncoder.cpp" "$GEN/host/VkDecoder.cpp"
python3 tools/diagnostics/gfxstream-surface-wire/compile_host.py "$GEN" /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json "$WORK/host"
python3 tools/diagnostics/gfxstream-present-results/compile_guest.py "$GEN" /var/tmp/rme-encoder-native-validation-v1 "$SOURCE" "$WORK/guest"
```

The wire test executes extracted generated request and response statements
against the real EMU Surface adapters with substitute Vulkan and window
functions. It checks the instance and boxed identity, missing-window failure,
and release; the output test also exercises successful destruction and an
instance mismatch. The full generated decoder and seven Guest encoder files
compile as Android ARM64 objects. No real Android Surface, decoder session,
GPU, device, or Guest has been executed. Guest dispatch, the actual service,
surface/swapchain lifetime synchronization and capability negotiation are still
missing, so this candidate must not be installed or activated.
