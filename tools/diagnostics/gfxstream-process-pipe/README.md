# gfxstream process pipe initialization failure

This patch targets the examined gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`. For the QEMU process pipe it
keeps initialization pending after an open, confirmation-write or UID-read
failure, so a later call can retry. `HostConnection::connect` now reports a
failed process-pipe setup instead of continuing to create a connection with no
process identity. A QEMU descriptor of 0 is valid; -1 is the invalid sentinel,
including during restart. The no-render-control case still avoids a pipe.

`run.py` copies both upstream files into a temporary directory and compiles the
actual extracted process initialization and restart functions plus the actual
connection call-site tail, with fake pipe and host dependencies. On the examined
unpatched source seven cases fail. Applying `fix.patch` passes open, write, read,
retry, normal, no-render-control, connection failure/retry, fd 0 and restart
without a pipe. It does not exercise actual Android networking, a GPU, full
HostConnection construction or the separate virtio transport implementation.

```sh
python3 tools/diagnostics/gfxstream-process-pipe/run.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --patch tools/diagnostics/gfxstream-process-pipe/fix.patch \
  --cxx /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ \
  --compiler-arg=-nostdlib++ --compiler-arg=-fno-exceptions
```

Omit `--patch` to observe the failures. `compile_stream_components.py` applies
the patch with no fuzz to temporary copies and compiles both full Android ARM64
sources. This was run using the local encoder command template and Android 16
headers; the diagnostic strict link also succeeded against the existing local
objects and read-only Guest link libraries. This establishes no Guest runtime,
Vulkan presentation, or GPU functionality. Keep the upstream copyright and
licenses when using the source.
