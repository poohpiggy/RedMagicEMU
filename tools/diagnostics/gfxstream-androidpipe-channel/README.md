# AndroidPipe local channel experiment

This is an isolated integration experiment, not a Guest Vulkan implementation.
It registers the same upstream `opengles` and `GLProcessPipe` services and
forwards open/send/receive/poll/close through the same upstream AndroidPipe
functions. The upstream OpenGLES service creates the real RenderChannel only
when an initialized renderer exists; the local test deliberately does not
initialize one or access a GPU. The process-pipe test exercises the real
service's ID allocation and close path, but no graphics resources are created.

All calls and Session destruction belong to one initializing thread; each
upstream operation holds the same real recursive VM lock passed to AndroidPipe.
The selected aemu library omits `VmLock.cpp`, so the verification executable
links that unmodified source explicitly. The socket server takes a caller-owned,
already-bound listener at the instance's `/dev/socket/aemuvk` path and requires
a peer-authorization callback; it does not create the listener or implement
instance authentication. Its `SO_PEERCRED` PID/UID/GID are inputs to that
callback, not proof that a peer belongs to a particular Guest. Do not connect
it to Guest traffic until the owner supplies verified per-instance
authorization and listener isolation. Connections and buffers are bounded.

After the supplied authorization callback succeeds, `SocketSession::peer()`
returns the same connection's kernel-provided credential snapshot for the
session's lifetime. This is an input to later binding, not proof of a Guest
process or Vulkan instance owner; it is not yet propagated to AndroidPipe,
RenderThread or VkDecoder. In particular, a PID alone is not a durable
identity. `verify_peer.sh <build-root> <ndk-root> <AndroidPipe.o> <out>` runs
the existing local socket/ProcessPipe test with a peer-value assertion and
cross-compiles `socket_session.cpp`; it does not initialize a renderer or GPU.

The upstream AndroidPipe threading initialization installs no-op timer
callbacks. `waker_patch.py` creates a separate copy of the upstream source with
an additional timer-injection function, without changing the original function.
The adapter supplies an eventfd-based wake queue: its owner must poll the
borrowed `wakeupFd()` alongside its transport and invoke `dispatchWakeups()`
on the device thread, under the adapter's VM lock. The local test first
reproduces a lost cross-thread wake with the original implementation, then
confirms that the modified real AndroidPipe delivers it on the device thread
under the VM lock. This is a local wake test, not evidence of correct
RenderChannel close, wake, or lifecycle behavior under GPU load. The GLProcessPipe API
requires an aligned complete 4-byte confirmation with value 100 and an 8-byte
receive buffer; this adapter rejects other sequences and copies the input to
aligned storage. The socket transport buffers partial selectors, confirmation
and output and forwards the original opengles payload including the flags.

To run against the existing fixed offline source and libraries, create an
isolated output directory and invoke `verify.sh <build-root> <ndk-root> <out>`.
It runs the Linux tests with the actual libraries and cross-compiles both the
adapter and modified upstream AndroidPipe objects for Android ARM64. For the
test executable, the modified AndroidPipe object interposes the same-version
shared-library implementation. A product build must instead compile and link
the modified AndroidPipe exactly once; it must not mix implementations. The
Android backend still has unresolved native GPU
info/safety symbols; this check does not link or run that backend on Android.

`renderer_start.cpp` contains the same-revision upstream initialization call
sequence and requires caller-supplied VM, window, display and feature objects;
it refuses unconfigured/non-host selection and checks the existing GPU
blacklists before starting. Its local test exercises only failure before GPU
startup. In particular this is **not** a solution for the missing Android GPU
info provider or for tying its checks to the physical device the renderer
selects. It neither connects a Surface nor produces Guest swapchain images.

For an offline strict integration-link check, first generate the candidate
device-identity object with the `gfxstream-device-uuid/verify.py` command
documented there. Then run:

```sh
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-androidpipe-channel/relink_android.py \
  /var/tmp/rme-gfxstream-build.hTnuwsWV \
  /var/tmp/rme-aemu-android-wrapper.53oSJ2Tb/build-shared \
  /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI \
  /var/tmp/rme-pipe-channel.w3wvezNf/VkCommonOperations-identity-android.o \
  /var/tmp/rme-pipe-channel.w3wvezNf
```

Each run writes a new isolated output directory, printed as `OUTPUT`. The aemu
host-common library links strictly and the adapter objects compile. The final
backend link is **expected to fail** with exactly five unresolved symbols:
`getGpuInfoListNative(GpuInfoList*)`, `rme::nativeGpuInfoResultFor(...)`,
`isVulkanSafeToUseNative()`, `rme_set_renderer_headless`, and
`gfxstream::vk::getApprovedVulkanIdentity(...)`. The script exits successfully
only when that expected failure is reproduced; it does not produce a working
backend or validate Android execution. The exact UUID object can be regenerated
with the four-argument command in the UUID experiment's README.

The optional UUID experiment can instead be omitted while retaining the
previously compiled merged physical-GPU selection checks. Supply the verified
merged object (not the composer's different original archive member):

```sh
python3 /mnt/e/RedMagicEMU/tools/diagnostics/gfxstream-androidpipe-channel/relink_android.py --without-uuid \
  /var/tmp/rme-gfxstream-build.hTnuwsWV \
  /var/tmp/rme-aemu-android-wrapper.53oSJ2Tb/build-shared \
  /var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI \
  /var/tmp/rme-d7a-vk-merged-android.IFuEeE2h/build/host/vulkan/liblib_vulkan_server.a.p/VkCommonOperations.cpp.o \
  /var/tmp/rme-pipe-channel.w3wvezNf
```

This mode checks the exact SHA-256 of the already verified merged object, which
retains physical-device type, Vulkan version and combined graphics/compute queue
checks. The resulting strict link was verified to fail on the unresolved
`getGpuInfoListNative`, `rme::nativeGpuInfoResultFor`, and
`isVulkanSafeToUseNative` and `rme_set_renderer_headless`. The original UUID mode also requires its UUID provider.
No GPU provider is substituted, and neither result is a runnable Android backend.

`renderer_bootstrap.cpp` is a candidate ordering for a separately owned native
window: start renderer, synchronously check `android_showOpenglesWindow`, then
and only then invoke an atomic service-publication callback. On show failure it
stops the renderer; on publication failure it hides the window then stops the
renderer. A controlled-substitute test checks the order and cleanup results,
and the source cross-compiles for Android ARM64. Its contract requires callers
to exclude every other Guest/ASG entry before publication and to close the
service before renderer teardown. It does not prove the upstream internal
threads have stopped or make the global `sInitialized` error state safe. This
is a FrameBuffer-owned host surface, not a Guest Vulkan instance's surface; no
Guest swapchain, application lifecycle, or actual renderer is connected.

The direct Guest swapchain experiment must not use that FrameBuffer window.
The renderer start parameters now include `headless` (default false), which
configures the fixed upstream renderer before creation to leave subwindow
ownership to the Guest's own host Vulkan instance. `verify_headless.py` builds
this isolated source and tests its configuration guard. It does not configure
the Guest's surface, disable the host GLES build, or establish a working Vulkan
renderer. See the Android surface README for the reproducible link commands.

`primary_display.cpp` supplies display 0 width, height and density from
explicitly supplied `VmSettings` values, and enumerates that single primary
display. It reports multi-display disabled only for the target NX809J HWC,
which itself reports one primary display; the local test extracts and runs
that actual HWC query. The caller must explicitly bind `SinglePrimary`
topology; a requested `PixelFold` topology is rejected, and `isPixelFold` is
not supplied before a valid topology is bound. The caller must also explicitly
select `NonFoldable`; unknown or foldable modes are rejected. The resulting
window agent reports not folded and has no folded area. These values describe
the Guest configuration, not the physical host phone's fold state. Stop and
join the renderer before clearing the binding, since upstream callbacks do not
carry a session identity. Its binding is
scoped by both an internal token and the caller's `VmLifecycle` generation;
stale sessions cannot update or clear the current binding. It reports failure
if asked for position, flags or enabled state through `getMultiDisplay`, which
these inputs do not supply. For the pose interface, it records the renderer's
explicit single-display origin `(0, 0)` and DPI (including zero) only when the
requested size matches the bound framebuffer. A request for a resize or other
position fails. Creating the already-bound display 0 is idempotent; attempts to
create another display or destroy the bound primary display fail. This state is
renderer metadata and does not change the physical host display. No Kotlin
caller or product integration is installed by this experiment.

When explicitly enabled for a bound display, the optional ColorBuffer callbacks
associate display 0 only with objects returned by the same renderer's
`FrameBuffer::findColorBuffer` registry. They retain only a weak reference while
associated, reject missing/reused handles and perform registry lookups outside
the agent lock. Each callback holds a lease until all of its temporary strong
references are gone. `quiescePrimaryColorBuffers` rejects new callbacks and
returns true only after those leases drain. Local tests exercise concurrent
quiesce while a weak reference is promoted, as well as the actual extracted
`findColorBuffer` body with a substitute registry and substitute objects; they do
not validate GPU resources. `verify_color_registry.py <android-build-dir> <out>`
cross-compiles the production lookup against the fixed actual FrameBuffer header.
`stopRenderer` first quiesces the callbacks, calls the real upstream renderer
stop and only then clears the display binding. A local concurrent test exercises
this sequence with a substitute registry and no renderer; a separate test calls
the actual upstream stop with no renderer present and reports `NoRenderer`.
The sequence has not been tested with a real FrameBuffer, renderer or GPU and is
not connected to the product. Nothing here connects a Guest gralloc handle,
fence, swapchain, or image to that ColorBuffer.

`vm_snapshot.cpp` provides only `setSkipSnapshotSave` and
`isSnapshotSaveSkipped` when the caller explicitly binds a VM whose snapshot
capability is unavailable. It records the upstream request, but effective skip
remains true because that VM has no snapshot save operation; save/load function
pointers are absent rather than reporting success. The current `GuestVm.stop()`
performs sync and process cleanup and has no VM snapshot save call; `VmPaths`
only declares an otherwise unused snapshots path. This source audit is not a
runtime proof for future VM modes. Because the upstream callback has no session
argument, the owner must stop and join the previous renderer before clearing or
rebinding; tokens protect explicit bind/read/clear calls, not a stale renderer
callback. No Kotlin caller or product integration is installed.

The fixed Android source does not dispatch `PostWorker` tasks to an Activity's
UI Looper: `postOnlyOnMainThread()` is false and the Vulkan worker also uses
false. The verification script extracts and tests the actual fixed-source
worker path, and checks the guarded screenshot call sites. The candidate
renderer entry therefore does not require `runOnUiThread` or
`isRunningInUiThread` on Android; the condition remains on other platforms.
This does not provide Guest composition or a display output.

## Android debug renderer startup

The optional Debug bundle starts the paired Android Vulkan renderer as a child
of the target EMU VM. It uses the existing VM display settings and VM
shutdown protocol. The child reports ready only when gfxstream has a live
Vulkan device and records the selected device in the `RedMagicEMU-Vulkan`
logcat tag. This startup exercise has no Guest connection. The headless
build also handles `PostCmd::Exit` with no presentation worker.

Build the bundle from the paired backend produced by the
[single-process binding](../gfxstream-vulkanonly-rc/README.md#single-authenticated-process-binding):

```sh
REPO=/mnt/e/RedMagicEMU
TOOLS=/var/tmp/rme-gfxstream-build.hTnuwsWV
BUILD=/var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV/build-fixed
PAIR="$REPO/build/diagnostics/rme-vulkan-bound.m05FAXjz/backend"
OUT=$(mktemp -d "$REPO/build/diagnostics/rme-vulkan-startup.XXXXXXXX")
python3 "$REPO/tools/diagnostics/gfxstream-androidpipe-channel/build_renderer_service.py" \
  "$TOOLS" "$BUILD" "$PAIR" "$OUT/renderer"
```

Run `:app:assembleDebug` with `-PexperimentalVulkanJniDir=<absolute path to
renderer/jniLibs>`. The build script verifies the staged ARM64 interpreter,
dependencies and LOAD alignment and writes `renderer/sha256.json`. Logging
in the VM shows renderer readiness or a startup error. Test on a GPU-enabled
NX809J instance by starting the VM, checking the actual GPU in logcat and
stopping the VM to verify the child exits. Rebuild without the Gradle property
to remove the startup bundle. The build uses the original Guest ROM and
engine assets.

The separately retained `android_ui_agent.cpp` / `ui_dispatcher.cpp` prototype
has process-terminating failure paths and must not be connected to EMU or run
as part of this verification. Upstream's void callback cannot report a failed
enqueue; a failed callback cannot safely be treated as completed. Actual Android
Looper execution was never tested, and this prototype has no app/JNI entry point.
