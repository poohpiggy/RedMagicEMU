# Single-session renderer drain candidate

The upstream renderer creates a worker before checking `mStopped` in
`createRenderChannel`. If shutdown wins that race, the new channel can be
destroyed without closing its queues, while its destructor waits for the
worker. `fix-create.patch` serializes the stopped check and channel creation
under the existing renderer lock. The real Android `Thread::start()` creates a
thread without waiting for its work to finish; nevertheless, this lock-order
change requires a real integration test before deployment.

`add-drain.patch` adds a candidate `rmeDrainForSession()` to stop new channel
creation, stop each existing channel and wait for its render worker. The
intended single-session order is: close admission of new work, invoke drain
outside the AndroidPipe VM lock, close the actual process pipe, wait for process
cleanup, then stop the renderer. Calling renderer `stop(true)` before process
pipe closure stops its cleanup thread, so it is not a substitute for this
order. The caller and authorization are not implemented here; actual process
identity, socket teardown, multi-thread behavior and GPU quiescence remain
unverified. This candidate is not installed or integrated into the app.

`test_create.py` extracts the actual upstream method into the existing
controlled-worker harness. The baseline hangs after renderer shutdown; the
patched code passes already-stopped, normal and creation-versus-stop cases.
`test_drain.py` exercises the candidate method with a controlled blocking worker
and the actual upstream GLProcessPipe via the local adapter; the baseline fails
and the candidate waits for the worker before closing the pipe. It does not
instantiate an actual RenderThread, bind the two sockets, or initialize a GPU.
The full patched Vulkan-only `RendererImpl.cpp` compiles as an Android ARM64
object; that is not a linked renderer.

```sh
SOURCE=/var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV/source
ROOT=/var/tmp/rme-gfxstream-build.hTnuwsWV
PIPE=/var/tmp/rme-pipe-channel.w3wvezNf/AndroidPipe.o
python3 tools/diagnostics/gfxstream-renderer-channel-drain/test_create.py "$SOURCE" "$ROOT/bin/cxx" # expected failure
python3 tools/diagnostics/gfxstream-renderer-channel-drain/test_create.py "$SOURCE" "$ROOT/bin/cxx" --patch tools/diagnostics/gfxstream-renderer-channel-drain/fix-create.patch
python3 tools/diagnostics/gfxstream-renderer-channel-drain/test_drain.py "$SOURCE" "$ROOT" "$PIPE" # expected failure
python3 tools/diagnostics/gfxstream-renderer-channel-drain/test_drain.py "$SOURCE" "$ROOT" "$PIPE" --patch tools/diagnostics/gfxstream-renderer-channel-drain/add-drain.patch
python3 tools/diagnostics/gfxstream-renderer-channel-drain/compile_android.py "$SOURCE" /var/tmp/rme-d7a-android-vulkanonly.dW2NKFQV/build-fixed/compile_commands.json /var/tmp/rme-renderer-drain-new
```
