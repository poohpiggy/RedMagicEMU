# Strict camera frame receiver

Archived in 0.5.0: this prototype was removed from the working tree. Its
original source and tests are preserved under
`E:\RedMagicEMU-checkpoints\0.4.0-camera-20261115\before-user-batch-small-prune`.
The details below describe that archived copy.

`camera_read.c` is an independent protocol component; it is **not connected**
to gralloc, the renderer, or any shipped APK. The header specifies its exact
little-endian request and response. The caller supplies an already-connected,
exclusively held stream, destination capacity/stride, id, sequence and 128-bit
nonce. The peer must echo the complete identity, sequence, dimensions, and
payload length; only status 0 followed by one complete RGBA frame succeeds.
Status 1 reports no frame, status 2 reports server failure, and any unknown
status is a protocol error. Failure statuses have no payload. Any failure,
timeout, disconnect, malformed reply or incomplete frame fails. On any negative
return, the caller must close the stream and may retry on
a new connection. Connection establishment is outside this component.

The transfer has a single monotonic deadline, handles partial I/O and EINTR,
and prevents SIGPIPE. Destination rows are copied only after complete receipt;
padding is preserved. The destination must not be read or written concurrently
by another thread during a commit. Dimensions are limited to 8192 and frame
payloads to 64 MiB; timeout is from 1 to 60000 ms. The caller owns nonce
generation and must establish its uniqueness and lifecycle with the eventual
host implementation. Echo verification alone does not prove uniqueness across
processes, allocator restarts, or hostile peers.

From the checkpoint root, run `sh native/grallocwrap/tests/run_camera_read.sh`
on a POSIX host to exercise
real socketpairs with injected partial I/O and interruptions, two frames,
stride padding, mismatched identities, malformed responses, EOF, timeout,
closed peers, invalid inputs, and a fresh connection after failures. Passing
this test does not establish Guest integration, gralloc error propagation,
GPU fence semantics, production artifact contents, or device behavior.
