# GLES buffer rebind regression

`run.sh` compiles the production encoder through the shared in-memory harness
in `../gles-bridge-local-test/map_test.c`. It checks that a repeated buffer name
is sent to the host for ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER and an element
buffer on a nonzero VAO, after a second shared context deletes and reuses the
name. It also checks that float and integer vertex attribute pointer calls are
resent after an array buffer rebind, at indices 0, 7, 15 and 16, while repeated
attribute calls remain cached without an intervening array buffer bind. It uses
no device or socket. Run on Linux or WSL:

```sh
sh tools/diagnostics/gles-buffer-rebind/run.sh
```

`mesa_test.c` is a separate native EGL/GLES comparison. It creates two shared
contexts, keeps the old object bound in the first, reuses its name in the
second, and verifies that rebinding the name selects the new object. A draw
test also checks that vertex attributes retain the old object until their
pointer is set again, then use the new object's data. It needs
EGL and GLES headers and libraries, plus an EGL implementation able to create
a GLES 3 context. For example, on a Mesa system with development packages:

```sh
cc -std=c11 -Wall -Wextra mesa_test.c -lEGL -lGLESv2 -o mesa_test
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 ./mesa_test
```

An implementation is not required to immediately reuse a name: the Mesa test
returns 3 if reuse does not occur. These tests check the bind command and native
driver behavior separately; they do not exercise the emulator host decoder,
Android application rendering, or buffer-mapping correctness.
