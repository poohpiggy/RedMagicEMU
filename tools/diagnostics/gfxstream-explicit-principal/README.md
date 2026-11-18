# Explicit host pipe principal experiment

This isolated, host-only registry maps an opaque **host-owned** AndroidPipe
hardware-pipe address to a caller-provided shared principal. Binding is
exclusive and its RAII lifetime removes the entry. A captured principal stays
allocated after unbind; rebinding the same address produces a new mapping.
Null pointers, null principals and duplicate live bindings are rejected.
No Guest bytes or self-reported PUID are used to construct an identity.

This registry does not authenticate a client, issue a revocable authorization
lease, wait for requests or workers, or prove that a captured principal remains
authorized after its connection closes. Do not use it to allow a Vulkan
surface request. The independent source token in the tests is a fake; the
actual EMU authorizer has not supplied a principal.

```sh
ROOT=/var/tmp/rme-gfxstream-build.hTnuwsWV
NDK=/var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
OUT=/var/tmp/rme-explicit-principal-new
mkdir -p "$OUT"
CXX="$ROOT/bin/cxx" sh tools/diagnostics/gfxstream-explicit-principal/test_registry.sh "$OUT"
sh tools/diagnostics/gfxstream-explicit-principal/test_connector.sh "$ROOT" "$NDK" /var/tmp/rme-final-host-l83ea6k9/AndroidPipe.o "$OUT"
```

The first test checks registration, duplicate rejection, address reuse,
concurrency and shared-object lifetime. The second uses the actual
AndroidPipe connector with a substitute service/renderer to test explicit
parameter capture before starting a worker, a legacy empty-principal path and
address reuse after close. It does not execute real EmuglPipe, RenderThread or
VkDecoder. The registry is also compiled as an Android ARM64 object.

Integrating this design requires a same-source rebuild of the Renderer virtual
interface and all implementations, verified authenticated ownership, and a
connection teardown protocol that revokes new use, stops the real worker and
drains in-flight operations outside VM locks before rebinding. Upstream
instance cleanup is currently tracked globally and via PUID cleanup callbacks,
not by this registry. No decoder, socket authorization, product interface or
Guest was modified by this experiment.
