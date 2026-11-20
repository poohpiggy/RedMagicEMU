# NX809J camera compatibility

The API 36 virtual HAL reports a fixed post-RAW sensitivity boost range of
`[100, 100]` for both cameras. The NX809J camera app dereferences this range
while constructing its ISO settings. The HAL does not advertise RAW output or
adjustable post-RAW boost. Android's metadata documentation says the range is
normally null without RAW; this fixed value is an intentional compatibility
deviation and does not establish that preview or photography works.

The HAL also declares an empty list of available session keys. The camera app
iterates that list without checking for null; the empty list accurately states
that this HAL exposes no session-specific controls. Its effect through the
provider and framework must still be checked on the target guest.

The API 36 HAL omits the `xiaomi.hdr.hdrChecker` vendor key from its exported
list: that full name is also a vendor section name, so Android's longest-prefix
lookup cannot resolve the key. Its original entry remains in the source table;
the other exported tag IDs and the nested section are retained. This omitted
key is not available to camera clients.

The HAL registers the Qualcomm `iso_available_modes` vendor key as int32 so
the NX809J app can query it, but supplies no characteristic value and does not
advertise manual ISO support. The app's observed null-value path disables its
absolute manual ISO setting.

The Qualcomm object-tracking `TrackerStatus` result key is registered as
int32 for the NX809J focus UI, but no value is reported and object tracking is
not advertised. The app's observed null-value path treats tracking as absent.

On a later target-guest run the stock camera reached its preview renderer but
failed when `HardwareBufferRenderer` attempted to attach a
`GL_TEXTURE_EXTERNAL_OES` texture to a framebuffer: the driver reported
`GL_INVALID_OPERATION` and `GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT`.
The host camera was opened and frames were sent, but the run did not validate
guest preview or capture. A rendering-compatible path remains to be established;
the HAL metadata changes above do not fix this graphics failure.

An independent Guest GLES 2 diagnostic using synthetic RGBA buffers with the
stock camera's observed `0x33` usage reproduced the external-texture FBO error
on the GPU bridge. In the same run, a 2D alias of the same EGLImage produced
matching red and green pixels on the GPU, OES sampling, `AHardwareBuffer_lock`,
and `AHardwareBuffer_lockPlanes` across two new buffers. A separate controlled
probe showed that translating only the FBO target still left CPU reads stale;
the host buffer association and Guest readback synchronization are also needed.
The reproducible diagnostic is in `tools/diagnostics/ahb-gl-fbo-native/` and is
expected to fail until the actual external-texture path is fixed. This evidence
does not establish a complete or safe implementation.

On 2026-11-15 the matching experimental host and Guest graphics libraries were
installed in the existing instance with the user's authorization. The same
synthetic probe passed OES attachment, two changing frames and CPU readback on
two new buffers; the user confirmed front and rear preview in the stock camera.
Still capture showed a storage-preparation message and has not been validated.
The experimental path is not a production fix: readback failure, resource
lifetime and restart handling remain incomplete. See
`tools/diagnostics/oes-fbo-prototype/README.md` for
test scope and rollback.

On the connected target, running that independent probe through the existing
SwiftShader-only process policy also failed: its external FBO attachment gave
`0x500`/`0x8cd7`, and the 2D control's quadrant checks did not match. Changing
the camera process to software rendering alone is therefore not a verified fix.

The EMU host source now refuses to send a GPU buffer when its requested frame
has not arrived or its existing EGL fence wait fails or times out. The focused
`native/glbridge/tests/buf_read_test.py` regression covers these cases, including
spurious wakeups and a later arriving frame. This source change is not yet
packaged or installed. A missing fence still follows the previous behavior,
and the existing Guest read protocol does not distinguish all failures from an
unchanged buffer; this step alone does not complete the camera synchronization
or fix the stock app's FBO attachment.

The host readback source also checks AHardwareBuffer unlock before sending a
frame and releases a successful lock even if its address is null. The focused
`buf_read_test.py` covers both failure cases. This remains unshipped: the old
guest protocol cannot distinguish a readback failure from an unchanged buffer,
and the stock app's external texture attachment still fails.

The HAL source now returns a Camera3 request error instead of reporting an old
or synthetic black preview frame when no new frame arrives within its existing
wait. A mixed JPEG and preview request waits for a frame after receiving the
JPEG and requesting preview restart; an in-flight frame could still satisfy the
sequence check. It also retains the buffer handle value in the request
queue. `tests/no_stale_frame_test.c` exercises these cases with controlled frame
arrival and callbacks. This change has only been tested on the host and built
as a temporary ARM64 library: the APK assets and installed app have not been
updated. In particular, the 300 ms preview timeout and mixed capture behavior
still need testing with a real host camera, and successful-request fence and
flush handling remain separate work. This change does not fix the stock camera
renderer crash.

On the subsequent target-device still attempt, the stock app received a JPEG
callback but crashed because it dereferenced a missing `SENSOR_EXPOSURE_TIME`.
Its following ISO calculation also dereferences `SENSOR_SENSITIVITY` and
`CONTROL_POST_RAW_SENSITIVITY_BOOST`. The HAL source now supplies nominal
virtual values (33,333,333 ns, ISO 100 and boost 100) and declares these result
keys. These are compatibility values for the virtual camera, not measurements
from the host sensor. `tests/exposure_result_test.c` checks their declarations
and returned values. Full host exposure/ISO propagation remains unimplemented.
For isolated on-device testing the experimental HAL was built from a temporary
source copy that keeps the previously installed request/queue behavior; it does
not contain the unshipped no-stale-frame changes described above. The real
photo save, returned image contents and error recovery remain unverified.

After registering the optional `org.quic.camera2.statsconfigs.AECLuxIndex`
Float key without supplying a lux measurement, a new target-device experiment
reached the JPEG storage success callback. The user confirmed that the stock
gallery opens a photo. The running build remains experimental; the nominal
exposure and ISO above are not host measurements, and image orientation,
failure recovery and the complete Camera2 path have not been verified.

`native/grallocwrap/tests/gpu_fetch_test.c` is a separate failing regression:
an interrupted transfer can partly overwrite a buffer and still report a
successful lock. No gralloc production fix has been included because a later
read can still return an old frame as an ordinary no-update result.

A separate strict frame receiver and socketpair regression were prototyped under
`native/grallocwrap/camera_read.*` and `native/grallocwrap/tests/`. They check
frame identity and sequence, receive a complete frame before copying it, and
return errors for timeouts and incomplete transfers. This is not connected to
the renderer or gralloc, and no host implements its protocol yet. Buffer nonce
creation and lifetime, GPU frame publication, and a compatible metadata layout
still require verification before integration; it is not in the installed APK. The prototype was removed from the working tree during 0.5.0 pruning; sources and tests remain in the before-user-batch-small-prune checkpoint.

Rebuild the two `n64-36` camera HAL assets from `camera_hal3.c` with NDK
28.2.13676358 and `camera_tags_29.h` (the latter preserves the existing vendor
tag table), using `build3.sh 36 camera_tags_29.h` in a compatible shell. Build
the APK afterwards so its generated engine asset index is refreshed. At guest
startup TreeFixer copies changed engine assets into the runtime image; the
pre-existing runtime file may be moved to its parked-files directory if no
parked file already exists. That parked file is not necessarily an original
firmware file: in the observed instance both parked camera HALs were the
previous EMU assets. The imported source firmware remains separate.

To roll back, stop the guest, restore the prior camera HAL source and both
`n64-36` camera HAL assets from a known previous revision without discarding
unrelated changes, rebuild and install that APK, then start the guest so
TreeFixer replaces the runtime copies. Do not delete the guest or its original
firmware to roll back.

## 2026-11-15 0.6.0 camera regression check

- The installed 0.6.0 settings build (`app-standard-debug-battery-live-signed.apk`,
  SHA-256 `d8f16fc1f6ff05ff6c56783c9e6cd7130a0e76d76111a9c8cc70c8c2a66dc0a4`)
  reproduced the stock-camera crash in Guest `vpwzbf`. DropBox records six
  `com.android.camera` crashes, all with the same stack:
  `HardwareBufferRenderer.onDraw -> GLCanvas.beginRenderTarget ->
  GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:8cd7`.
- This is the known external-texture/EGLImage framebuffer path, not a failure in
  the new battery, sensor, GPS or settings code. The host bridge repeatedly
  logged that the back camera opened, so camera permission/HAL delivery got far
  enough to reach preview rendering.
- The boot log also contains one `vendor.camera-provider` SIGSEGV in
  `camx.provider-impl.so`; later guest camera requests still opened, and the
  application crash has the independent GL stack above. This provider failure
  must be tracked separately and is not used as the sole explanation.
- The current production graphics assets do not include the temporary
  OES-framebuffer prototype that previously made front/rear preview and one
  still capture work. Re-merging that experiment into a new 0.6.0 APK remains a
  separate change: the prototype still has unresolved readback, lifetime,
  disconnect and frame-synchronization limitations.
- A package-level audit against the user-accepted 0.5.0 APK then identified the
  exact regression scope: all 128 non-engine APK paths were unchanged; all 205
  engine paths existed in both packages; only eight engine entries and two
  native entries differed. Six engine/native differences were expected 0.6.0
  work (stubs, sensors, radio, Wi-Fi and the network-gated API 36 engine). The
  other four were camera-related files that had fallen back to older assets:
  host `libaemugl.so`, Guest `libGLES_aemu.so`, and both system/vendor camera
  HAL copies.
- Those four files were restored byte-for-byte from
  `RedMagicEMU-0.5.0-beta-delete-stub-clean.apk`: `libaemugl.so` SHA-256
  `990c361f73fe9760a2644d7a139a3e4fe2377f22a054298f87883656b3332533`;
  Guest GLES blob `db987d74570f64213a7076bb3ae3b83a30c37d30`; camera HAL blobs
  `1c2e48bc07a08d5367c5efdf5456d3b42d6cbfbe`. After rebuilding, the 0.6.0 APK
  contains those exact accepted assets. The user tested front/rear preview in
  the installed 0.6.0 build and confirmed it works.
