# Local regression for the production GLES map encoder

This test compiles the **complete current** `native/glbridge/guest.c` directly
into a test executable and calls its actual context-creation, buffer-binding,
map, pointer, explicit-flush, unmap and delete encoder functions. It does not
change the product source or assets, connect to a device, or use a socket.

The only platform dependencies replaced are Android logging/property calls and
the encoder's low-level `read`/`write`: a memory-only test host returns context
IDs, buffer names, fixed query replies and a simulated error value. The harness
decodes emitted write commands into per-object byte arrays. The test uses the
production encoder to generate, bind and allocate buffers. Shared contexts are
created through the production `eglCreateContext`; the harness selects the
encoder's current context directly.

The test host is not OpenGL: it does not validate invalid parameters as a host
GL driver would, nor does it validate actual driver state, GL error ordering,
disconnect behavior or rendering. Therefore the invalid-parameter cases expose
what happens with the fixed mock replies, not the complete behavior of the real
host GL path. Successful tests are necessary but not sufficient for release or
for claiming Antutu is fixed.

Run on Linux or WSL with a C compiler and `timeout`:

```sh
sh /mnt/e/RedMagicEMU/tools/diagnostics/gles-bridge-local-test/run.sh
RME_SANITIZE=address sh /mnt/e/RedMagicEMU/tools/diagnostics/gles-bridge-local-test/run.sh
```

The runner creates a fresh `/var/tmp/rme-real-gles-map.*` directory and prints
its path. It runs each case separately with a timeout and returns 1 if any case
fails. It never interprets a failing case or a timeout as success. AddressSanitizer
is optional; it requires a compatible host compiler/runtime.

The original six cases cover two mapped objects bound in turn to one target;
one mapped object observed via two targets; use from another shared context;
destruction of the original shared context; nonzero mapping and explicit-flush
offsets with per-object byte checks; and delete/name reuse. Additional cases
cover an old object and a new object with the same name in shared contexts,
queries of mapped state, access flags, offset and length, and seven invalid
mapping parameter combinations.

In the last run against the current checkout, the original six cases failed,
as did `mapped-state` and all seven `invalid-*` cases. `reused-name-shared`
passed under the memory-only test host. The runner returned 1. These results
describe that run, not an assertion about future code changes or real GLES.
