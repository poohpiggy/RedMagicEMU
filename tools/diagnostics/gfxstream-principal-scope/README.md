# Host principal scope experiment

The noncopyable, thread-local RAII scope holds a host-provided shared principal
for one synchronous consumer. An inner scope masks its outer scope even when
empty; destroying the inner scope restores the outer state. `take()` consumes
only the current scope and returns empty when missing or already consumed.
The object is a token supplied by a trusted host caller, **not** authentication
and not a value accepted from Guest wire data, a PUID or a PID.

This is an isolated experiment. The actual source chain from
`android_pipe_guest_send` through `ConnectorPipe::onGuestSend` to
`Service::create`, `EmuglPipe` and `RendererImpl::createRenderChannel` is
synchronous for a normal selector. `RenderChannelImpl` starts its worker during
construction, so a future integration must capture before that start. Snapshot
and other renderer paths must not inherit unrelated authority. Multiple guest
connections can share a token only after a real host authorizer establishes
their common process identity and lifetime.

```sh
ROOT=/var/tmp/rme-gfxstream-build.hTnuwsWV
NDK=/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
OUT=/var/tmp/rme-principal-scope-new
mkdir -p "$OUT"
CXX="$ROOT/bin/cxx" sh tools/diagnostics/gfxstream-principal-scope/test_scope.sh "$OUT"
sh tools/diagnostics/gfxstream-principal-scope/test_connector.sh "$ROOT" "$NDK" /var/tmp/rme-final-host-l83ea6k9/AndroidPipe.o "$OUT"
```

The first test covers missing and already-consumed scopes, nesting, exception
unwind, two threads, and retained lifetime. The second uses the actual
AndroidPipe connector and VM lock with a substitute service/renderer that
captures before starting a worker. It checks absent identity, one-shot
consumption, failure and lifetime after scope exit. It does **not** instantiate
the actual EmuglPipe, RenderChannelImpl or VkDecoder. A local Linux test also
links two different shared modules to one principal-scope shared library and
checks that they see the same TLS. The Android ARM64 carrier is only compiled.

No Android product linking or linker namespace was tested. A product must
ensure the adapter and renderer use exactly one shared implementation; two
statically embedded copies create independent TLS. The scope alone does not
authenticate an existing socket, prevent descriptor handoff, determine which
Guest app owns a connection, or prevent an unscoped same-thread reentrant call
from consuming the ambient token. Before integration, bind consumption to the
intended channel, prove actual renderer call ordering and error propagation,
and arrange an independent EMU-owned window. Nothing here enables WSI, sends a
surface request, touches a phone or produces a Guest frame.
