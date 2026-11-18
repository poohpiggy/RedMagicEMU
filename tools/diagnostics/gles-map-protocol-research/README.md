# Local host-map protocol research — NOT PRODUCT CODE

This standalone experiment tests a possible RedMagicEMU graphics bridge direction on a local EGL/GLES driver. It does not modify, replace, or connect to the repository bridge, any Android app, firmware, device, or the existing EMU wire protocol. It is not safe to ship or install. It only handles `WRITE | INVALIDATE_RANGE | FLUSH_EXPLICIT | UNSYNCHRONIZED` (`0x36`), and does not replace existing handling for any other access flags.

## Prerequisites and execution

Linux/WSL with a C11 compiler, pthreads, Python 3, `libEGL.so.1`, Mesa surfaceless EGL and an OpenGL ES 3 context. No GLES development headers are needed: the test loads EGL and GL entry points using `ctypes` and `eglGetProcAddress`. The WSL host used for this run was Mesa 26.0.8 llvmpipe, EGL 1.5 / OpenGL ES 3.2. Driver and results can differ on physical GPUs. The installed Mesa attempted `/dev/dri` and printed permission warnings, then used software rendering; no permissions or security settings were changed. Run:

```sh
sh ./run.sh
```

The script copies these source/test files into a fresh temporary directory, compiles the host and guest test libraries with `-Wall -Wextra -Werror`, runs the success-path tests and all safety regressions, and removes the temporary build directory. **The safety regressions currently fail, so the runner returns nonzero.** An AddressSanitizer-capable compiler is required for the use-after-free case. This directory deliberately contains no compiled binaries, private logs, app code, or firmware.

## What tests cover

`test_pm.py` exercises the isolated host API with an actual Mesa ES context: two simultaneous maps, current-bound-object checks, cross-target flush, shared-context access, preservation of one previous GL error, and a host range error. `test_socket.py` connects a C guest-mirror module to a Python test server over a real local socket, maps two buffers through the host C module, flushes data from the local mirrors, draws red and green triangles with the mapped VBOs, and verifies framebuffer pixels. The Python server is a test driver, not an EMU host implementation. `test_decoder_guard.c` exercises an independent proposed decoder-lock contract: when all participating operations use one outer lock, flush and deletion are serialized; after a successful backend delete, a trusted object-generation event invalidates the old mapping ID, while a failed delete does not. This is a controlled mock contract and is not integrated into `host.c`. `test_wire_faults.py` verifies truncated/invalid replies and lost unmap responses are not reported as success. `test_faults.c host_map_rollback` supplies a deliberately contradictory fake host map that returns a pointer and GL error, and verifies the candidate attempts rollback and preserves that GL error. Two additional mock cases check that host flush and unmap errors are returned and recorded instead of being reported as success.

## Known release-blocking failures and gaps

- Host mapping pointers are treated as object identity. Delete/recreate and pointer reuse (ABA) can pass validation for the wrong buffer. A mutex inside the prototype does not serialize existing EMU GL bind/delete/unmap calls, so another host thread can invalidate a pointer between validation and `memcpy` (use-after-free). The separate `guard.c` contract takes an outer decoder mutex before `pm`'s private mutex, and has a caller-supplied, post-delete invalidation event. It is only effective if every conflicting decoded command, bridge-internal temporary buffer operation, and relevant context/lifecycle path uses the same correct lock and supplies the exact deleted object generation. Existing `host.c` does neither; lock ordering against its `g_lock` and `g_win_lock` has not been proven, and the original ABA/race/UAF tests intentionally remain failing.
- A context or connection can disappear with live maps. The design does not know how to find and unmap every corresponding host object under a valid context, or recover safely after a response is lost. `pm_free` fails when maps remain; this prevents a false cleanup claim but is not a lifecycle solution. The new `pm_group_retire` only clears bookkeeping after its caller has independently confirmed that the last real EGL context of that group was destroyed; it rejects registered live contexts and prevents a retired group ID from being reused. The independent test models that confirmation; no production decoder supplies it, and neither connection loss nor `pm_ctx_free` alone proves it. Guest retains an uncertain mirror on a lost unmap reply; process exit is not a general recovery design.
- GL error collection is only wired to `pm_error` in the test. Existing EMU `glGetError` does not consume this queue. Correct error ordering, all host-side GL calls, context loss, and existing pending errors need complete integration and tests. A contradictory pointer-plus-error backend, failed rollback, or error-queue overflow must not result in a success reply.
- The Python server and local socket format are test-only. They are not the production decoder, have no compatible version negotiation, and do not establish general framing, maximum-size validation, authenticated ownership, protocol recovery, or coexistence with existing bridge messages.
- Group identity is supplied by test code rather than proven from EGL share-group lifetime. VAO binding tracking, name generations, all buffer deletion routes and contexts, and mixed old/new map paths are not integrated. Host API lookup by pointer is inadequate even if the local tests pass.
- Read maps, non-invalidated write maps, write-only partial preservation, other flags, GL buffer state queries, correct range/OOM error semantics, and all API extensions remain unsupported. A nonzero GL error, a lost reply, or an unknown state must never be treated as successful.
- The accepted camera bridge transformations and production host/guest libraries have not been changed or tested with this protocol. The repository `tools/diagnostics/oes-fbo-prototype/build.py` can recreate the accepted camera-enabled assets; any eventual integration must preserve that behavior and verify the transformation anchors.

`test_faults.c` documents two deliberately failing cases, `pointer_reuse` and `disconnect`. To reproduce them without treating failure as success, compile locally with `cc -std=c11 -Wall -Wextra -Werror -pthread pm.c test_faults.c -o /tmp/rme-pm-faults` and run `/tmp/rme-pm-faults pointer_reuse` and `/tmp/rme-pm-faults disconnect` separately. Both currently return nonzero. Do not install this experiment or use it as a proposed fix while these failures remain.

## Independently reproduced safety failures

`test_pm_safety.c` deliberately models host code paths that bypass the prototype's private lock. It demonstrates why the successful socket and Mesa tests are not a release criterion. On the reviewed prototype, `aba`, `race`, `lifecycle`, and `errors` each return nonzero, and the `uaf` case run under AddressSanitizer reports `heap-use-after-free` in `pm_flush`. Run each separately so the actual failing exit status remains visible:

```sh
cc -std=c11 -Wall -Wextra -Werror -pthread pm.c test_pm_safety.c -o /tmp/rme-pm-safety
/tmp/rme-pm-safety aba
/tmp/rme-pm-safety race
/tmp/rme-pm-safety lifecycle
/tmp/rme-pm-safety errors
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address -fno-omit-frame-pointer -pthread pm.c test_pm_safety.c -o /tmp/rme-pm-safety-asan
/tmp/rme-pm-safety-asan uaf
```

These failures must remain visible. They are a reason not to integrate this code, not passing validation. The race and UAF tests deliberately emulate other host connections that do not take the prototype's private mutex; their behavior demonstrates the missing whole-server synchronization contract.
