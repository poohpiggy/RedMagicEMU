# Host OpenCL debug probe

The existing Debug-only log-panel button runs the original host GPU computation
and then an OpenCL discovery round trip over an in-process `socketpair`. The
second result only enumerates devices: it does not run a kernel or connect a
Guest. A CPU device is reported as a CPU device, using the actual OpenCL device
type bitfield. The displayed version is reported by the host driver; it is not a
claim that an EMU OpenCL implementation supports that version.

`discovery.c` loads `libOpenCL.so` by its public soname, queries platform and
device IDs and information, and converts handles to non-reused process-local
32-bit IDs scoped to one session. The server accepts an already-connected FD;
it creates no listener or network endpoint. This debug transport does not
authenticate a Guest, and must not be connected to one without a separately
reviewed authorization and lifecycle design.

Requests are four little-endian 32-bit words: magic `0x31444352`, operation,
session ID and parameter. Replies are magic, signed status, payload length and
payload. Payloads are bounded to 4096 bytes; IDs and numeric information are
little-endian, strings include their NUL terminator. Supported operations are
platform enumeration (1), selected platform strings (2), all-device
enumeration (3), selected device strings (4), and device type or compiler
availability (5). All other requests fail. The client prefers a reported GPU,
and otherwise reports the first device without changing its type.

Negative OpenCL statuses are passed through; private statuses -32001 through
-32005 identify loader, symbol, size/limit, transport and protocol errors.
This protocol is exploratory and does not implement OpenCL's complete ABI,
memory objects, synchronization, or GPU compute on behalf of a Guest.

Build the Android Debug library with `build.ps1 -Ndk <NDK path>` and run the
Linux/WSL regressions with `python3 native/opencl-host-probe/tests/test_probe.py`
and `python3 native/opencl-host-probe/tests/test_discovery.py`. A real Linux
OpenCL implementation is required for `tests/verify_real_discovery.py`: set
`LD_LIBRARY_PATH` to an existing clvk library and dependencies first. The
script compares direct queries with the socket result using Mesa llvmpipe and
reports it as a software CPU. Fake library tests cover error handling but are
not evidence of GPU execution.
Calls to a vendor driver can block or crash the hosting process.
