# Swapchain decoder generator candidate

This offline experiment adds the three swapchain decoder hooks from the
isolated metadata and image-registration candidates to the pinned d7a
`scripts/cereal/decoder.py` generation flow. The generated decoder records
successful swapchain creation, removes metadata on destruction, and handles
registered image enumeration. It retains the existing gfxstream wire framing;
it does not implement version negotiation or present-result transport.

The baseline regenerated decoder fails the image-registration test. The
candidate-generated decoder passes the existing CPU substitute test, including
`VK_INCOMPLETE`, stable boxes, collision rejection and driver-owned cleanup,
and compiles to an Android ARM64 object with the matching candidate state
header. A normalized comparison of the baseline and candidate generated trees
finds differences only in these three decoder cases; generated-path comments
are ignored in that comparison.

This is not integrated into a product build. The underlying candidates still
have raw-image ownership and authorization limitations and must not be exposed
as WSI. No GPU, Android device, Guest, real Surface or full decoder was run.
The generator requires locally available fixed-version source, Python tooling,
the pre-existing build inputs and clang-format.

```sh
SOURCE=/var/tmp/rme-gfxstream-build.hTnuwsWV/src
NDK=/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
python3 tools/diagnostics/gfxstream-present-results/generate.py "$SOURCE" /var/tmp/rme-generator-baseline-new --clang-bin "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
python3 tools/diagnostics/gfxstream-swapchain-generator/generate.py "$SOURCE" /var/tmp/rme-generator-candidate-new --clang-bin "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
python3 tools/diagnostics/gfxstream-swapchain-generator/verify_scope.py /var/tmp/rme-generator-baseline-new /var/tmp/rme-generator-candidate-new
python3 tools/diagnostics/gfxstream-swapchain-images/run.py "$SOURCE" --patch tools/diagnostics/gfxstream-swapchain-images/fix.patch --decoder /var/tmp/rme-generator-candidate-new/host/VkDecoder.cpp
python3 tools/diagnostics/gfxstream-swapchain-generator/compile_android.py /var/tmp/rme-generator-candidate-new /var/tmp/rme-swapchain-images-arm64-7kj7d528/out/host/vulkan /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json /var/tmp/rme-generator-android-new
```

The candidate state header in the last command is a local intermediate created
by the Android compile recipe in `../gfxstream-swapchain-images/README.md`;
it is not committed or a self-contained SDK.

`--surface-present` additionally combines the matching surface query codecs
and paired present-results experiment in the same generation run. The original
swapchain-only output fails both the surface command check and present-results
test; the combined output passes those checks, all current surface output and
allocator substitute tests, and the swapchain image-registration substitute
test. A first decoder compile failed because it found old cereal headers; the
compile helper now prefers headers produced by the same generation run.

Seven Guest encoder objects and nine matching host objects (decoder, state,
snapshot and six cereal sources) compile as Android ARM64. A relocatable merge
of the host objects resolves the Surface serialization and snapshot symbols;
this is not a final renderer link. The paired present experiment changes the
wire response and has no connected version negotiation. Surface creation,
ownership, real rendering and output remain unimplemented, so these files
must not be connected, packaged or exposed as WSI.

```sh
GEN=/var/tmp/rme-wsi-combined-new
STATE=/var/tmp/rme-swapchain-images-arm64-7kj7d528/out/host/vulkan
DB=/var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json
python3 tools/diagnostics/gfxstream-swapchain-generator/generate.py "$SOURCE" "$GEN" --clang-bin "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin" --surface-present
python3 tools/diagnostics/gfxstream-surface-wire/check.py "$GEN/guest/VkEncoder.cpp" "$GEN/host/VkDecoder.cpp"
python3 tools/diagnostics/gfxstream-surface-wire/test_outputs.py "$GEN/guest/VkEncoder.cpp" "$GEN/host/VkDecoder.cpp"
python3 tools/diagnostics/gfxstream-present-results/test.py "$GEN"
python3 tools/diagnostics/gfxstream-swapchain-images/run.py "$SOURCE" --patch tools/diagnostics/gfxstream-swapchain-images/fix.patch --decoder "$GEN/host/VkDecoder.cpp"
python3 tools/diagnostics/gfxstream-present-results/compile_guest.py "$GEN" /var/tmp/rme-encoder-native-validation-v1 "$SOURCE" /var/tmp/rme-wsi-combined-guest-new
python3 tools/diagnostics/gfxstream-swapchain-generator/compile_android.py "$GEN" "$STATE" "$DB" /var/tmp/rme-wsi-combined-host-new
python3 tools/diagnostics/gfxstream-swapchain-generator/compile_components.py "$GEN" "$STATE" "$DB" /var/tmp/rme-wsi-combined-components-new
python3 tools/diagnostics/gfxstream-swapchain-generator/link_components.py "$NDK" /var/tmp/rme-wsi-combined-host-new/VkDecoder.o /var/tmp/rme-wsi-combined-components-new /var/tmp/rme-wsi-combined-new.o
```

The tests require a local host C++ compiler. All listed `/var/tmp` output names
must be new. The generator and `apply_fixes.py` operate on that isolated tree.

`--surface-present --surface-create` additionally generates the experimental
paired `vkCreateEmuSurfaceRME` message in the same registry as the surface,
swapchain and present commands. It takes a boxed instance and does not transfer
a Guest window pointer. The generated host decoder calls the EMU-owned window
adapter, and its compile helper includes the adapter's header when needed.
The new command must not be exposed until protocol negotiation, Guest dispatch,
instance ownership and Surface/swapchain teardown are connected.

With the same variables and prerequisites above, use a new output directory:

```sh
GEN=/var/tmp/rme-wsi-create-new
python3 tools/diagnostics/gfxstream-swapchain-generator/generate.py "$SOURCE" "$GEN" --clang-bin "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin" --surface-present --surface-create
python3 tools/diagnostics/gfxstream-surface-wire/check_create.py "$GEN/guest/VkEncoder.cpp" "$GEN/host/VkDecoder.cpp"
python3 tools/diagnostics/gfxstream-surface-wire/test_create_wire.py "$GEN" /var/tmp/rme-gfxstream-build.hTnuwsWV "$NDK"
python3 tools/diagnostics/gfxstream-surface-wire/test_outputs.py "$GEN/guest/VkEncoder.cpp" "$GEN/host/VkDecoder.cpp"
python3 tools/diagnostics/gfxstream-present-results/test.py "$GEN"
python3 tools/diagnostics/gfxstream-swapchain-images/run.py "$SOURCE" --patch tools/diagnostics/gfxstream-swapchain-images/fix.patch --decoder "$GEN/host/VkDecoder.cpp"
python3 tools/diagnostics/gfxstream-present-results/compile_guest.py "$GEN" /var/tmp/rme-encoder-native-validation-v1 "$SOURCE" /var/tmp/rme-wsi-create-guest-new
python3 tools/diagnostics/gfxstream-swapchain-generator/compile_android.py "$GEN" "$STATE" "$DB" /var/tmp/rme-wsi-create-host-new
python3 tools/diagnostics/gfxstream-swapchain-generator/compile_components.py "$GEN" "$STATE" "$DB" /var/tmp/rme-wsi-create-components-new
mkdir /var/tmp/rme-wsi-create-adapter-new
sh tools/diagnostics/gfxstream-surface-wire/test_host_guest_surface.sh /var/tmp/rme-gfxstream-build.hTnuwsWV "$NDK" /var/tmp/rme-wsi-create-adapter-new
sh native/vulkanbridge/tests/test_surface_cpp_link.sh /var/tmp/rme-gfxstream-build.hTnuwsWV "$NDK" /var/tmp/rme-wsi-create-adapter-new
python3 tools/diagnostics/gfxstream-swapchain-generator/link_components.py "$NDK" /var/tmp/rme-wsi-create-host-new/VkDecoder.o /var/tmp/rme-wsi-create-components-new /var/tmp/rme-wsi-create-host-new.o --surface-adapter-dir /var/tmp/rme-wsi-create-adapter-new
```

These checks pass with the pinned local inputs: the generated create wire,
surface outputs, present results and image registration pass substitute tests;
seven Guest and nine host objects compile for Android ARM64. A relocatable
merge including the three adapter objects resolves the EMU surface adapter and
generated surface serialization symbols. It is not a final renderer link or
an Android runtime/GPU test. The private create command and modified present
response have no connected protocol negotiation, so this remains isolated.

The same combined code can also be checked against the previously built
Vulkan-only backend. Compile the decoder and all eight support objects with
that build's own compile database, then provide those objects and the three
surface-adapter objects to `link_vulkanonly.py`. The script substitutes exactly
one copy of each decoder, state, snapshot and cereal member into new thin
archives; it keeps the original Vulkan-only GPU selection object and checks
the known source hash. It rejects candidate objects whose recorded compile
commands do not use `GFXSTREAM_ENABLE_HOST_GLES=0`. It does not change or
provide `isVulkanSafeToUseNative()`.

```sh
VKO=/var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV
VKO_DB=$VKO/build-fixed/compile_commands.json
python3 tools/diagnostics/gfxstream-swapchain-generator/compile_android.py "$GEN" "$STATE" "$VKO_DB" /var/tmp/rme-vko-wsi-host-new
python3 tools/diagnostics/gfxstream-swapchain-generator/compile_components.py "$GEN" "$STATE" "$VKO_DB" /var/tmp/rme-vko-wsi-components-new
python3 tools/diagnostics/gfxstream-swapchain-generator/link_vulkanonly.py /var/tmp/rme-gfxstream-build.hTnuwsWV "$VKO/build-fixed" /var/tmp/rme-vko-wsi-host-new/VkDecoder.o /var/tmp/rme-vko-wsi-components-new /var/tmp/rme-wsi-create-adapter-new "$VKO/gpuinfo" /var/tmp/rme-vko-wsi-strict-new
```

With the pinned inputs the strict link has exactly one unresolved symbol:
`isVulkanSafeToUseNative()`. This confirms these selected objects can be linked
up to that existing platform-provider gap; it does not establish a compatible
runtime ABI, a usable backend or GPU rendering. Generated code, modified
present framing and the private create opcode remain unnegotiated and must not
be deployed or exposed as WSI.

After applying the isolated per-channel negotiation candidate described in
`../gfxstream-wsi-negotiation/README.md`, the same link checker may also use
its matching Vulkan-only render-thread object by adding:

```sh
--render-thread /var/tmp/rme-wsi-protocol-new/renderthread/RenderThread.o
```

The object must have been compiled with `--wsi-negotiation`; the checker
requires its negotiated-decoder reference and Vulkan-only build flags. With
the pinned inputs the combined negotiated backend still has only the real
`isVulkanSafeToUseNative()` provider unresolved. The render thread still lacks
the actual process-pipe binding and the linker result is not a runnable build.

The generator also corrects swapchain creation output handling. The Guest
request writes a zero placeholder instead of reading the caller's output;
the host writes zero on driver failure; the Guest reads the result before
creating a handle mapping and rejects a success response with a null handle.
The response wire order is unchanged. `test_create_failure.py` executes the
actual generated request, host writeback, and Guest response statements: all
three cases fail on the earlier generated output and pass with this change.
The test also exercises success, nonzero output on failure, and an invalid
success with a null output. It uses a synthetic wire and driver, not a Vulkan
driver or Android runtime:

```sh
CXX=/var/tmp/rme-gfxstream-build.hTnuwsWV/bin/cxx python3 tools/diagnostics/gfxstream-swapchain-generator/test_create_failure.py "$GEN"
```

For `vkGetSwapchainImagesKHR`, count-only requests now send a zero placeholder
instead of reading the output count. Requests with an image array send the
input capacity but zero-fill output handle placeholders. This preserves the
wire layout. `test_images_request.py` runs the real generated request against
count and array sentinels; both fail before the change and pass afterward.

```sh
CXX=/var/tmp/rme-gfxstream-build.hTnuwsWV/bin/cxx python3 tools/diagnostics/gfxstream-swapchain-generator/test_images_request.py "$GEN"
```

The candidate response now caches each Guest image wrapper by swapchain and
host-boxed image, reuses it across successful and incomplete enumerations, and
releases the Guest wrappers when the swapchain is destroyed. The host retains
ownership of its swapchain images. Error responses do not create wrappers. If
the host returns more images than the supplied capacity, the encoder drains
the returned IDs in fixed-size chunks before reading the result, reports an
error, and does not modify the application's image array. Its test uses actual
generated response statements, a fragmented substitute stream, allocation
tracking and a trailing sentinel; the previous candidate failed the test.

```sh
CXX=/var/tmp/rme-gfxstream-build.hTnuwsWV/bin/cxx python3 tools/diagnostics/gfxstream-swapchain-generator/test_images_response.py "$GEN"
```

The tests use substitute mappings and do not validate actual ResourceTracker
locking, GPU images, Guest image creation metadata or Android runtime behavior.
WSI remains unavailable and this generated candidate must not be deployed.

The modified Guest encoder and matching Vulkan-only host decoder compile to
Android ARM64 objects. Existing Surface, present and swapchain image substitute
tests pass; these do not establish a functioning Guest GPU path.
