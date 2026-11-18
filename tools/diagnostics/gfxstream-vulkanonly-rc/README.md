# Vulkan-only renderControl initialization candidate

This is an isolated candidate for the fixed gfxstream d7a revision. The
Vulkan-only RenderThread does not compile the upstream GLES renderControl
decoder, while the Goldfish/QEMU-pipe guest initializes it before Vulkan.
The candidate implements only the no-checksum wire forms of
`rcGetHostExtensionsString`, `rcGetRendererVersion`, and `rcSetPuid`.
Other renderControl opcodes are rejected. It advertises **no** Vulkan, WSI,
sync, gralloc, or GLES capability and does not run a GPU or renderer.

`run.py` extracts the actual generated guest encoder functions, rather than
building request packets by hand. The original/no-decoder path exits with a
failed handshake; the candidate passes string, version, PUID, fragmented and
invalid packets. The `--real-pipe` variant creates the actual upstream
GLProcessPipe through the existing local AndroidPipe adapter, gets its actual
PUID, feeds it to the encoder, and verifies process-pipe closure. This is a
controlled in-process test: it does not prove either Binder or socket
authorization, Vulkan execution, real fence behavior, or Android operation.

The candidate checks `rcSetPuid` against an independently supplied trusted
PUID. The caller must bind the same actual live process pipe to its render
channel; a value received from the Guest, a live ID alone, or a socket PID by
itself is not authorization. `compile_renderthread.py` creates a separate
patched RenderThread source and builds its Android ARM64 object from the
existing Vulkan-only compile database. The injected code supplies PUID zero
until that binding exists, and therefore closes the channel on `rcSetPuid`.
It is not a runnable integration and must not be used as a completed renderer.
The candidate is not installed and does not modify upstream sources in place.

```sh
ROOT=/var/tmp/rme-gfxstream-build.hTnuwsWV
SOURCE=$ROOT/src
python3 tools/diagnostics/gfxstream-vulkanonly-rc/run.py "$SOURCE" "$ROOT" # expected failure
python3 tools/diagnostics/gfxstream-vulkanonly-rc/run.py "$SOURCE" "$ROOT" --candidate
python3 tools/diagnostics/gfxstream-vulkanonly-rc/run.py "$SOURCE" "$ROOT" --candidate \
  --real-pipe /var/tmp/rme-pipe-channel.w3wvezNf/AndroidPipe.o
python3 tools/diagnostics/gfxstream-vulkanonly-rc/compile_renderthread.py \
  /var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV/source \
  /var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV/build-fixed/compile_commands.json \
  /var/tmp/rme-vulkanonly-rc-new-output
```

The local compile inputs are temporary and include earlier diagnostics; they
are not a distributable source tree. The original Android backend still does
not strictly link, and the candidate does not repair loading, swapchain images,
present, host-coherent mapping, Android native synchronization, or lifecycle
teardown. `ANDROID_EMU_vulkan` must not be advertised until its requirements
are actually satisfied.

## Single authenticated process binding

The compute-only build uses `--bind-process-pipe` in place of
`--wsi-negotiation`. The EMU caller supplies one principal and a peer
authorization callback for a single live Guest process. The socket server
records the ID from the actual host `GLProcessPipe`. A graphics channel
captures that principal before its worker starts. The worker accepts
`rcSetPuid` for the recorded ID and admits Vulkan packets while that
principal and its process resources are live.

On process-pipe disconnect the server closes admission and the graphics
sockets, then calls the owner's drain function outside the AndroidPipe VM
lock. The owner stops and joins render workers in this function. The server
retains the process pipe through the callback so its resources remain live.
The caller must complete the subsequent process cleanup before stopping the
renderer.

`native/vulkanbridge/client/test_pipe_integration.sh` verifies the actual
EMU client, Unix socket server and upstream process pipe with a local
same-process authorizer. It checks ID acceptance, mismatch rejection and
drain order. This test uses no live renderer.

For Android ARM64, use the paired backend command from
[`gfxstream-android-gpuinfo`](../gfxstream-android-gpuinfo/README.md). Keep
`--with-android-preflight` and replace the entire `--render-thread` option
with this option and its library:

```sh
--androidpipe-host-common \
  /var/tmp/rme-pipe-channel.w3wvezNf/rme-integrated-link-ge6q38g7/libaemu-host-common.so
```

The command recompiles RenderThread and its callers with one class layout,
then strict-links the socket adapter to the matching waker and VM lock.
Android runtime usage still needs an EMU session owner and a Guest client
loading entry point.
