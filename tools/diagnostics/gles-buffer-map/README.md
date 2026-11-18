# GLES 3 simultaneous buffer mapping diagnostic

The program creates its own EGL pbuffer and GLES 3 context, then allocates two
`GL_ARRAY_BUFFER` objects. It maps each for writing while the first remains
mapped, checks the pointer for each currently bound object, unmaps each, and
maps each for reading to verify that both byte patterns survived independently.
Separate contexts test that the same mapped object is visible from a different
binding target, duplicate maps fail with `GL_INVALID_OPERATION`, deleting a
mapped buffer does not leave stale state, and a shared map remains visible after
destroying one of two sharing contexts. A deleted-name reuse check runs only if
the driver returns that name when generating new objects; otherwise it prints
`SKIP` and does not establish name-reuse behavior.
The Unity-specific case maps two `GL_COPY_WRITE_BUFFER` objects with access
flags `0x36`, flushes each mapped range explicitly and checks the contents after
unmapping. It exercises a nonzero mapping offset.
Any missing context, GL error, null mapping, bad pointer, unmap failure, or
incorrect readback is a failure. The program deletes its objects and context.
This is a focused bridge regression, not proof of the cause of an application's
failure; the renderer string alone does not establish its actual route.

For the running intended Guest in the standard debug app, run from the repo root:

```powershell
python tools/diagnostics/gles-buffer-map/run.py --adb 'C:/Users/<user>/.redmagicemu-tools/android-sdk/platform-tools/adb.exe' --ndk 'C:/Users/<user>/.redmagicemu-tools/android-sdk/ndk/28.2.13676358' --serial '<adb-serial>' --image-id '<guest-image-id>'
```

The runner builds into a temporary local directory, stages a randomly named
executable inside the Guest, verifies its SHA-256 and uses the debug `SHELL`
receiver; it does not install or update an APK. The receiver needs an active
Guest activity. It removes the exact files created by the runner. Confirm the
Guest `aemu-gl` log identifies the probe and a GPU GLES 3 context; otherwise the
test may exercise a different driver. A timeout, missing completed output, or
nonzero `PROBE_EXIT` is failure. No device execution is implied by building it.

On 2026-11-16, the full diagnostic passed on the device's native Adreno driver
except that name reuse was skipped. Against the then-installed Guest GPU bridge,
the other cases failed. A temporary, narrowly gated bridge experiment passed
the Unity case and the benchmark produced results for both scenes; the other
cases still failed. The experiment is not a production fix and its runtime
library was restored. Respect the user's instruction to keep subsequent work
on the EMU side and not modify the Guest runtime.

The same probe also passed on WSL Mesa llvmpipe (EGL 1.5 / GLES 3.2), including
the name-reuse case. This verifies the probe's control behavior; it does not
exercise the EMU bridge. WSL could not access `/dev/dri` and used software
rendering without any permission changes.

On this Windows/WSL setup, the computer-only control can be repeated with:

```powershell
wsl.exe --exec sh /mnt/e/RedMagicEMU/tools/diagnostics/gles-buffer-map/run_mesa.sh /mnt/c/Users/<user>/.redmagicemu-tools/android-sdk/ndk/28.2.13676358
```

This requires a C compiler and the WSL `libEGL.so.1` and `libGL.so.1` runtime
libraries; it copies only the NDK's EGL/GLES headers to a temporary directory.
