# Per-render-channel WSI protocol negotiation candidate

This offline candidate negotiates the private Surface-create opcode and the
modified per-swapchain present response on the same gfxstream byte stream as
the Vulkan decoder which consumes them. It does not alter either existing
socket selector or authorization callback, and does not treat a protocol
version or the Guest-supplied process ID as authorization.

The Vulkan-only RenderControl candidate may advertise only the explicit
`ANDROID_EMU_rme_wsi_protocol_v1` negotiation marker when both the paired
decoder code is linked and that render thread actually has a Vulkan decoder.
The marker is not Vulkan, WSI, fence, or GPU support. Without it, the client
sends no negotiation packet and waits for no response. With it, the real
candidate `HostConnection::vkEncoder()` sends a version packet on the same
stream, records the accepted result on that encoder, and only that decoder
instance stores the corresponding version. The generated Surface create and
present encoder methods refuse to send without negotiation. Host Surface
creation is gated on the decoder's state; a non-negotiated host present keeps
the original aggregate-only response format, while a negotiated present also
returns per-swapchain results.

With `RME_EXPERIMENTAL_REMOTE_SURFACE`, the actual client adapter implements
`rme_android_surface_wire_ready()` by retrieving the calling thread's current
`HostConnection` and its paired encoder, then consulting that encoder's
negotiated state. A null instance, missing connection or unnegotiated encoder
returns false. The adapter also forwards present to that encoder, preserving
its overall and per-swapchain results. The CPU adapter test uses substitute
connections and encoders and verifies the state is not shared between test
threads; it does not prove a complete Android execution or bind an instance
to an authorized process.

`test_integration.py` checks the generated source integration and compiles the
actual generated decoder switch and present-response statements into a CPU
harness. It covers older services, exact marker matching, write/read failure,
independent channels, version rejection, and old/new present response framing.
The RC harness uses substitute streams; neither test executes the complete
Vulkan decoder, real socket service, Surface, Vulkan driver, or GPU. The
default RC constructor still disables the marker, and the candidate render
thread still uses a trusted PUID of zero until the existing socket/process
pipe binding is actually connected. This is not a runnable integration and
must not be deployed or used to expose WSI.

Given the existing pinned local inputs, prepare a fresh combined generation:

```sh
SOURCE=/var/tmp/rme-gfxstream-build.hTnuwsWV/src
ROOT=/var/tmp/rme-gfxstream-build.hTnuwsWV
NDK=/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
DB=/var/tmp/rme-d7a-android-platform.zk1aiJOj/aosp-build.AIhRB6VI/build-composer/compile_commands.json
STATE=/var/tmp/rme-swapchain-images-arm64-7kj7d528/out/host/vulkan
WORK=$(mktemp -d /var/tmp/rme-wsi-protocol.XXXXXXXX)
GEN=$WORK/generated
python3 tools/diagnostics/gfxstream-swapchain-generator/generate.py "$SOURCE" "$GEN" --clang-bin "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin" --surface-present --surface-create
python3 tools/diagnostics/gfxstream-wsi-negotiation/apply.py "$GEN" "$SOURCE/guest/OpenglSystemCommon/HostConnection.cpp" "$WORK/client"
python3 tools/diagnostics/gfxstream-wsi-negotiation/test_integration.py "$GEN" "$WORK/client/HostConnection.cpp"
python3 tools/diagnostics/gfxstream-vulkanonly-rc/run.py "$SOURCE" "$ROOT" --candidate --real-pipe /var/tmp/rme-pipe-channel.w3wvezNf/AndroidPipe.o
python3 tools/diagnostics/gfxstream-swapchain-generator/compile_android.py "$GEN" "$STATE" "$DB" "$WORK/host"
python3 tools/diagnostics/gfxstream-present-results/compile_guest.py "$GEN" /var/tmp/rme-encoder-native-validation-v1 "$SOURCE" "$WORK/guest"
python3 tools/diagnostics/gfxstream-wsi-negotiation/compile_connection.py /var/tmp/rme-encoder-native-validation-v1/android1-VkEncoder.command.txt "$SOURCE" /var/tmp/rme-aosp16-real-headers "$GEN/guest" "$WORK/client/HostConnection.cpp" "$WORK/connection"
sh native/vulkanbridge/client/test_remote_surface_encoder.sh "$SOURCE" "$NDK" "$WORK/adapter-test"
python3 native/vulkanbridge/client/compile_remote_surface_encoder.py /var/tmp/rme-encoder-native-validation-v1/android1-VkEncoder.command.txt "$GEN/guest" "$WORK/adapter" --wsi-negotiation
python3 tools/diagnostics/gfxstream-vulkanonly-rc/compile_renderthread.py /var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV/source /var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV/build-fixed/compile_commands.json "$WORK/renderthread" --wsi-negotiation
```

The output is isolated. Renderer link, per-instance connection authorization,
process-pipe identity/lifetime, real Android Surface teardown, Guest loader,
snapshot restore and complete rendering have not been verified. These tests
establish neither protocol compatibility with a real older running server nor
any Guest Vulkan frame. The modified present response must not be used without
the same per-channel agreement, and the generated entry points remain hidden.
