# Guest AHardwareBuffer / framebuffer diagnostic

This standalone native program tests the NX809J camera's observed `0x33`
CPU-read/write buffer usage on the running API 36 Guest. It allocates its own
RGBA buffers and does not open a camera or modify the stock camera app.

For each of two newly allocated buffers it tries to attach the external OES
texture to an FBO. It draws two different four-colour quadrant patterns, calls `glFinish`,
and checks GPU framebuffer readback, `AHardwareBuffer_lock` CPU readback,
`AHardwareBuffer_lockPlanes` with its reported row and pixel strides, and OES
sampling against each expected color and orientation. Each frame is read twice
with the stock camera's `0x33` lockPlanes usage before the plain CPU read. A separately allocated buffer using a
2D EGLImage alias is tested as a control. Any failed operation or pixel check
causes a nonzero exit. The program releases its own EGL and buffer resources.

On the pre-fix version observed on the target, the external attachment failed
with GL error `0x502` and FBO status `0x8cd7`; the 2D alias control returned
the expected pixels. A separate diagnostic that substituted a 2D target for
the external name demonstrated that fixing only attachment was insufficient:
GPU pixels changed, but CPU lock still returned old zeroes. A successful run
must show both cases and both colors passing; a complete FBO alone is not
enough. These historical observations do not establish a result for a new
build. On 2026-11-12 the strict diagnostic ran in the target Guest: both
external attachments failed with the same errors, while both 2D controls
passed both four-colour patterns for GPU, OES sampling, `lock`, and repeated
`lockPlanes` with usage `0x33`. The reported 10×6 buffer had rowStride 40 and
pixelStride 4, so this run did not exercise padded rows. The diagnostic exited
1 for the two failed external cases, as intended.

Build with Android NDK 28.2.13676358, for example from the repository root:

```powershell
$clang = 'C:/Users/<user>/.redmagicemu-tools/android-sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
& $clang --target=aarch64-linux-android29 '-Wl,-z,max-page-size=4096' -Wall -Wextra -Werror -O1 tools/diagnostics/ahb-gl-fbo-native/probe.c -o "$env:TEMP/rme-glfbo-probe" -landroid -lEGL -lGLESv2 -llog
```

For execution, start the intended Guest under the standard **debug** app and
identify its image ID. `run.py` compiles in a local temporary directory, uses a
random name for both device staging and the debug Guest shell output, verifies
the copied binary's SHA-256, and removes those exact temporary files afterwards:

```powershell
python tools/diagnostics/ahb-gl-fbo-native/run.py --adb '<path-to-adb>' --ndk '<path-to-ndk>' --serial '<adb-serial>' --image-id '<guest-image-id>'
```

The runner targets `app.redmagicemu`; it requires that app's existing Debug
`SHELL` receiver and a running Guest. It does not install or update an APK.
Check the Guest `aemu-gl` log for the probe process and its GPU context; the
renderer string alone does not establish the context route. An output timeout
or a nonzero `PROBE_EXIT` is a failure, not a passing graphics result.
