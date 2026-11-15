Archived from the working tree on 2026-11-15. Original source, tests and README are preserved in E:/RedMagicEMU-checkpoints/0.4.0-camera-20261115/before-offline-prototypes-cleanup/. The commands below apply to that archive.

# AOD loader decision experiment

This is an unconnected Android ARM64 interposer experiment. It neither starts,
installs, nor authorizes a replacement library. The `authorized` argument
must come from a future trusted launch context that verifies the exact target
firmware and APK; an APK path or process name alone is not identity.

The component considers only the exact NX809J SystemUI APK library path and an
explicit Android linker namespace. Other extension flags, including a supplied
library file descriptor, are passed to the original callback. Replacement
failure also calls the original callback. Its replacement path is reserved for
a future runtime-copy preparation step and is not currently populated.

Local test:

```sh
cc -std=c11 -Wall -Wextra -Werror tests/dispatch_test.c dispatch.c -o /tmp/rme-aod-dispatch-test
/tmp/rme-aod-dispatch-test
```

The dispatch test verifies callback arguments and a same-thread recursive
replacement. The ARM64 interposer preserves the incoming link register on
forwarding and uses a tail branch, but only synthetic ART/Bionic cases have
been checked. Production authorization is deliberately disabled. Test-only
compile switches may enable synthetic paths and must never be packaged.

The implementation binds directly to the target firmware's private
`__loader_android_dlopen_ext` symbol and passes the original caller address as
the fourth argument, as observed in its libdl. This avoids constructor-time
`dlsym` resolution. A missing or incompatible symbol may prevent the whole
process from starting. There is no reliable preload preflight and retry yet.
Exact firmware and APK selection, protection against replacement between
verification and use, real zygote namespaces, failure handling, and the
replacement library remain unverified. Do not connect this experiment to
normal startup.

An isolated target ART/Bionic test also found that a failed replacement load
followed by a successful original load changes the calling thread's `dlerror`
state. The available `dlerror` interface consumes the previous error and has
no supported setter. Clearing or reading it would discard existing caller
state; a successful preflight cannot guarantee that a later load will succeed.
The experiment must remain disabled until this difference is addressed without
silently discarding errors.
