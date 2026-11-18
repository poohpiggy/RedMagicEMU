# Vulkan public ABI candidate

This is a build-time generator for a non-HAL public Vulkan client ABI. It uses
the *matching* gfxstream d7a registry, generated Mesa dispatch trampolines,
and gfxstream entrypoint definitions as external inputs. Do not mix generated
files from a different revision. Upstream files retain their original copyright
and license notices; in particular the included Mesa generated trampoline file
retains Intel's MIT notice. This directory does not contain or modify a Guest
image, firmware, or a host system component.

The generator emits typed wrappers for Vulkan core 1.0–1.3 and selected Android,
surface, swapchain and promoted extensions. An optional newer Vulkan registry
adds core 1.4 names only when it declares an older command to be its alias, the
older Mesa trampoline exists, and command signatures and directly referenced
struct member layouts match after type aliases are resolved. Calls forward to the real Mesa
dispatch table / gfxstream globals. The exported `vkGetInstanceProcAddr` uses
Mesa's NULL-instance path, avoiding the fixed d7a NULL-handle undefined
behavior, and delegates normal queries to gfxstream. `vkGetDeviceProcAddr`
forwards to the device dispatch table. Exported symbol names alone do not
advertise support for any API version or extension; runtime query results and
actual backend implementation determine availability.

`vkCreateAndroidSurfaceKHR` and `vkDestroySurfaceKHR` direct dispatch now use
an EMU-owned Android window object, the instance allocator when no explicit
allocator is provided, and Android 16 native-window connect/disconnect calls.
Other surface handles return to the original Mesa destroy dispatch. Since image
acquisition, queueing/presentation and window swapchain integration are not yet
implemented, the candidate deliberately filters `VK_KHR_android_surface` and
`VK_KHR_swapchain` from extension enumeration. The direct symbols are not a
claim that applications can use the incomplete extensions.

Internal Android buffer-queue, slot-identity and one-image acquire/present
plumbing now calls the actual native-window callbacks and gfxstream driver
dispatch when supplied with real inputs. The acquire flow duplicates the
dequeue fence for the driver and retains the original for queue/cancel; the
present flow initializes the release fence to -1 and does not queue a buffer
if the driver reports failure. A native-image helper constructs the real
`VkNativeBufferANDROID` image-create chain from supplied buffer metadata and
forwards to gfxstream's device dispatch, propagating image errors; it does not
allocate or share the native buffer. The internal backed-swapchain object and
public dispatch use the flow, but the real image provider is not implemented;
`vkCreateSwapchainKHR` therefore returns `VK_ERROR_INITIALIZATION_FAILED` and
no swapchain extension is advertised. It does not create VkImages or provide
shared host ColorBuffers.
An isolated remote-surface pairing can now associate the local Android window
object with the Guest-side gfxstream handle returned by the paired private
surface-create command. On create failure it disconnects and releases the
local window; destruction sends the paired remote handle before releasing the
window. `android_remote_surface_encoder.cpp` calls the actual generated
`VkEncoder` create, surface queries, swapchain, acquire and present methods.
`test_remote_surface_encoder.sh` runs
the pairing and adapter with substitute connection/encoder objects; the
adapter also compiles against the matching generated Android ARM64 header via
`compile_remote_surface_encoder.py`. Under the otherwise disabled
`RME_EXPERIMENTAL_REMOTE_SURFACE` build flag, public surface creation and
queries, swapchain creation, image enumeration, destruction, single-chain
acquire and present use the paired handles only when the current channel's
negotiation says the private protocol is ready. The fallback keeps the existing
local-only path. The negotiation and the private commands are isolated
candidates and are not linked into the product. Guest-side repeated image
mapping, native synchronization, `AcquireNextImage2`
and real window teardown still need work. No WSI extension is enabled by this
candidate and no device or GPU has been used. The experimental path is tested
by setting `RME_REMOTE_SURFACE_TEST=1` for `test_dispatch.sh`.
If a backed swapchain's present fails, later acquire and present calls return
`VK_ERROR_OUT_OF_DATE_KHR` rather than retrying that queue. Destruction still
cancels acquired buffers. This is covered by the CPU-only object test, not by
a real Android window or GPU test.
`test_android_wsi.sh SOURCE BUILD OUTPUT` runs synthetic CPU-only ownership and
failure tests; it does not call the actual driver or use real fence descriptors.

Reproduce with a verified gfxstream source tree, its generated guest build,
matching AOSP headers and an Android NDK:

```sh
python3 test_public_api.py
sh test_dispatch.sh SOURCE BUILD /tmp/rme-client-test NEWER_VK_XML
sh compile_android.sh SOURCE BUILD AOSP_HEADERS NDK /tmp/rme-client-android NEWER_VK_XML
python3 check_ndk_exports.py NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm \
  NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/35/libvulkan.so \
  /tmp/rme-client-android/public.o
```

The host test exercises real generated Mesa trampolines with a substitute
dispatcher. It is a CPU-only ABI test, not a renderer or GPU test. The AArch64
compile checks signatures against the matching headers. A successful object
compile does not establish a working client shared library. The project still
needs a strictly linked matching Android Mesa runtime, WSI, gfxstream client and
transport, a real host decoder and a way to make the complete client available
before Guest zygote loads its Vulkan library. Encoder objects must match the
Android build configuration, including `GFXSTREAM_ENABLE_GUEST_GOLDFISH=1`, and
use actual Android headers. Do not use Mesa `android_stub` headers as evidence
of a production Android ABI. No Guest rendering has been demonstrated here.

A separate diagnostic rebuild of all 18 encoder sources with Goldfish enabled
and Android 16 AOSP `hardware.h` and `hwvulkan.h` completed without
using Mesa `android_stub` headers. The matching d7a Mesa runtime, util and
common/drm/headless WSI have also been cross-compiled with the Android platform
macro. After rebuilding HostConnection with the consistent Android flags, a
strict offline link succeeds using the real Android `libnativewindow` and
genuine stream, Goldfish, VirtGpu, GLES encoder and synchronization source
objects alongside read-only target Guest system libraries.
None of these objects has been executed on a device.
`strict_link_android.sh` records the exact link inputs; its arguments refer to
locally produced intermediate objects and archives, not committed Guest files.
The Mesa WSI pieces here do not implement Android window/swapchain integration.
The full configuration and link have not been validated as a production build.

The link is a diagnostic, not a runnable package. The public object, encoder,
three device fixes, ring buffer and same-ABI AOSP libbase property helpers can
be reproduced using the adjacent scripts; Mesa archives must be built from the
matching d7a source with `platforms=android`, API 35, the Android platform
macro, and genuine AOSP headers. The GLES sources are built by
`compile_gles_components.py`; their two encoder libraries and CodecCommon must
be linked separately with the real, read-only Guest system libraries. The
link also needs a matching Android AArch64 `libdrm`, the NDK, generated Mesa and
gfxstream headers, and the same-source Goldfish-enabled encoder/HostConnection
objects. Those inputs are local external build materials, not included in Git.

The examined source was d7a revision `d7a33801fb557cebd4f8564a8af431669b3f471d`,
but its local `gfxstream_vk_device.cpp` contains earlier TRACE instrumentation:
its SHA-256 is `6b53405174b89935f32086e6529d2e6cf65fe7666e18ecbcf50e522d195ec045`.
The d7a `vk.xml` SHA-256 is
`bc9a08e6a0c2b8ee4b32bb39aad27f827514dd582e460acbf1e446839c8b933c`,
and the newer registry SHA-256 is
`55ec60950cfb18c3575dcf5fd52741b2bb70eb1e466408f803a91049973ee6fb`.
The compiler was NDK r28c Clang 19.0.1, based on r530567e; Mesa's cross
compiler target was Android API 29 and its platform configuration API 35.
AOSP headers and libbase sources were from `android-16.0.0_r1`.

For the existing local inputs, these commands rebuild the client-side objects
and execute the strict link without interacting with a device:

```sh
CLIENT=native/vulkanbridge/client
SRC=/var/tmp/rme-gfxstream-build.hTnuwsWV/src
GEN=/var/tmp/rme-gfxstream-build.hTnuwsWV/build-guest
NDK=/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
AOSP=/var/tmp/rme-mesa-android-6ekppv9b
HEADERS=/var/tmp/rme-aosp16-real-headers
WORK=$(mktemp -d /var/tmp/rme-vk-client-build.XXXXXXXX)
ENC=$WORK/encoder
STREAM=$WORK/stream
GLES=$WORK/gles
OUT=$WORK/output
GUEST_LIBS=/var/tmp/rme-guest-link-libs-v9egnj_b
COMMANDS=$ENC
NEW_XML=$AOSP/src/src/vulkan/registry/vk.xml
python3 "$CLIENT/rebuild_encoder_candidates.py" "$SRC" "$GEN" "$AOSP" "$HEADERS" "$NDK" "$ENC"
python3 "$CLIENT/compile_stream_components.py" "$COMMANDS/android1-VkEncoder.command.txt" "$SRC" "$HEADERS" "$STREAM"
python3 "$CLIENT/compile_android_libbase.py" "$COMMANDS/android1-VkEncoder.command.txt" "$AOSP/aosp-libbase-files" "$STREAM"
python3 "$CLIENT/compile_gles_components.py" "$COMMANDS/android1-VkEncoder.command.txt" "$SRC" "$HEADERS" "$GLES"
sh "$CLIENT/compile_android_ring.sh" "$SRC" "$NDK" "$STREAM"
sh "$CLIENT/compile_android.sh" "$SRC" "$GEN" "$AOSP" "$NDK" "$OUT" "$NEW_XML"
sh "$CLIENT/compile_android_device.sh" "$SRC" "$GEN" "$AOSP" "$NDK" "$OUT" "$ENC/patched-source/guest/vulkan_enc"
"$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" -std=c11 -D_GNU_SOURCE -fPIC -Wall -Wextra -Werror -c "$CLIENT/emu_pipe.c" -o "$OUT/emu_pipe.o"
sh "$CLIENT/link_gles_android.sh" "$NDK" "$STREAM" "$GLES" "$GUEST_LIBS"
sh "$CLIENT/strict_link_android.sh" "$NDK" /var/tmp/rme-vk-client-mesa-repro "$ENC" "$OUT" "$STREAM" "$GUEST_LIBS" "$GLES" "$AOSP/build12/subprojects/libdrm-2.4.133/libdrm.so"
```

This is **not yet a fully self-contained build**: it depends on generated
gfxstream/Mesa headers, the Mesa cross file and `pkg-config` metadata, the
AOSP/NDK headers and source trees, and matching
Android ARM64 libdrm and read-only Guest libraries. `build_mesa_android.sh`
rebuilds all six Mesa archives from an empty build directory with matching
inputs; that was rerun successfully in `/var/tmp/rme-vk-client-mesa-repro`.
These sources and binaries are not committed, and these steps install nothing.

The AOSP property helpers, including `StringPrintf`, are hidden in the client
to avoid binding an NDK `std::__ndk1` return value to the platform libbase's
`std::__1` implementation. Static inspection found no imported `android::base`
or `std::__1` names in the four candidate libraries. Both NDK
`libc++_shared.so` and the Guest platform's `libc++.so` are nevertheless in the
dependency graph; names such as `std::exception` and `operator new` do not
carry that namespace distinction. Symbol interposition, exception/RTTI behavior,
library namespace visibility and actual loading remain unverified. No result
here establishes a functioning GPU transport, display, synchronization, or
Guest rendering; the diagnostic libraries must not be packaged as a release.

The encoder build also applies the fence-export failure fix: it does not report
success or discard the saved fence state when the Goldfish queue operation
fails. The check uses CPU substitutes; a functioning sync backend is still
required and no real fence has been tested.

The examined Guest `[system]` linker configuration marks the default namespace
isolated and searches `/system/${LIB}` and `/system_ext/${LIB}`. Its
`public.libraries.txt` lists `libsync.so`, but not `libcutils.so`, `libbase.so`,
`libdrm.so` or `libc++_shared.so`. This is configuration evidence, not an app
loading test: system namespace search paths do not establish classloader/JNI
namespace visibility, availability of these files at runtime, or where the
EMU-owned libraries can be loaded before zygote preloads Vulkan.

The API 35 NDK loader's 233 `vk*` names are present in the AArch64 object.
The Guest Android 16 loader examined separately has 251 `vk*` names. With the
optional newer registry the candidate forwards 13 additional core names to
verified older Mesa extension trampolines, leaving six names absent:
`vkCmdBindDescriptorSets2`, `vkCmdPushConstants2`, `vkCmdPushDescriptorSet2`,
`vkCmdPushDescriptorSetWithTemplate2`, `vkCmdSetRenderingAttachmentLocations`,
and `vkCmdSetRenderingInputAttachmentIndices`. An exported promoted name does
not establish that the corresponding extension is enabled or that Vulkan 1.4
is supported. Applications that directly link missing names cannot load this
candidate. Geekbench's actual symbol requirements have not been established.
