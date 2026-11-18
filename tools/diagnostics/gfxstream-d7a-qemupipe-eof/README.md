# gfxstream QEMU pipe zero-progress handling

This is a research patch against the locally examined gfxstream revision
`d7a33801fb557cebd4f8564a8af431669b3f471d`. In that source,
`qemu_pipe_read_fully` and `qemu_pipe_write_fully` continue looping when the
underlying read or write returns zero for a nonempty request. A closed stream or
zero-progress write can therefore leave the client spinning forever. The patch
returns `-EPIPE` for read EOF and `-EIO` for a zero-progress write. It preserves
short successful transfers, the existing retry policy and negative errors.

The test compiles the complete original functions and uses in-process fake
read/write/try-again functions; it does not open a socket, touch a device, or
execute Guest code. On the recorded source, both immediate and partial zero
read/write cases time out without the patch; after the patch, those four and
ten fragmentation/EINTR/EAGAIN/error/zero-length cases pass. The test assumes the
normal stream meaning of zero as EOF and does not establish a real emulator
pipe's behavior or transport compatibility. The existing retry mechanism can
still retry transient errors indefinitely; this patch does not add a deadline.

The upstream source is not included here. Preserve its Android Open Source
Project copyright and Apache-2.0 license and all other applicable notices if
applying or redistributing the patch. The scripts verify SHA-256 hashes of the
local source and its two QEMU pipe headers and modify only temporary copies.
They need the pre-existing source and NDK; they download nothing.

```sh
python3 tools/diagnostics/gfxstream-d7a-qemupipe-eof/run.py \
  --source /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  --patch tools/diagnostics/gfxstream-d7a-qemupipe-eof/fix.patch \
  --cxx /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++ \
  --compiler-arg=-nostdlib++

sh tools/diagnostics/gfxstream-d7a-qemupipe-eof/compile-android.sh \
  /var/tmp/rme-gfxstream-build.hTnuwsWV/src \
  /var/tmp/rme-ndk-r28c.jScP7TRf/android-ndk-r28c
```

Omit `--patch` from the first command to reproduce the expected failures. Two
independent Android ARM64/API 35 object builds had identical SHA-256
`253fe72040fe98491bc1df3a63af0da90af9a8335d786f31ea172c8a291325cf`.
The object is relocatable only, not a linked or runnable client.

This patch is not integrated into EMU. Guest application loading, the missing
graphics pipe/virtual GPU transport, host decoding and real GPU rendering are
still unimplemented. No phone, Guest file, imported package, or actual device
node is changed or accessed by these tests.
