# Shared buffer object lifetime experiment

This is a small, single-threaded reference model, not product code. It tracks
one name namespace per shared group, two buffer bindings per context, object
references and monotonically assigned model generations. The native GLES test
compares every modeled binding's name and size against a real EGL/GLES driver.
It covers deletion in another context, repeated name reuse while old objects
remain bound, deletion of a new object while an old same-name object is bound,
and creating a new shared context after destroying the original context.

The experiment registers each model object only after a successful native
`glGenBuffers`, `glBindBuffer` and `glBufferData`. This deliberately omits the
distinction between reserved names and objects for other sequences. It does
not handle VAOs, vertex or indexed bindings, errors, asynchronous commands,
concurrent contexts, mapping, data writes or emulator protocol behavior. It
must not be integrated into the bridge without those missing semantics.

On a Linux system with EGL/GLES development headers and libraries:

```sh
sh tools/diagnostics/gles-buffer-object-model/run.sh
```

`CPPFLAGS` and `LDLIBS` may specify alternate header and library paths. The test
returns 3 when the driver does not reuse the name within 4096 generated names;
that is an inconclusive result, not a passing test. It does not use a device,
socket, or Android environment. Mesa may probe graphics devices during EGL
initialization even when software rendering is requested.
