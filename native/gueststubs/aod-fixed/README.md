# Fixed-theme saliency experiment

The emulator can stage a proxy built from these sources for a freshly imported,
verified NX809J `IP_MC_W_CN` instance. It applies only to the one investigated
theme input described below; other inputs follow the failure or original-library
path. The original APK is retained. See [the AOD record](../../../docs/AOD_THEME_CACHE.md)
for runtime evidence, limitations, and rollback.
The proxy does not hold its state mutex while calling vendor code or JNI. A
generation counter and an immutable mask snapshot prevent an in-flight fixed
result from reviving state after release or a newer initialization. They do
not make separate JNI calls atomic or establish the original library's behavior
under concurrency. In particular, release can race between the final
generation check and writing the Java output array. Same-thread reentry during
library resolution is rejected rather than deadlocking; its effect on real
SystemUI's constructor error handling remains unverified.

The inspected NX809J SystemUI exposes six `SaliencyApi` JNI methods. Its
library stores the null-result byte and highest-point pointer globally, so this
experiment also maintains one latest fixed result per process. The target
release routine branches on the exported `isSoFileExist` byte; if true it
clears initialization status, the highest point and null-result state, and
if false it skips that cleanup. Original initialization can return `-1`.
The inspected uninitialized and empty-result paths set the null-result byte
without writing the previous highest point; this does not establish the
behavior of every other failure or exception path.
Those observations come from the specified local firmware, not from method
names. Exact behavior for all malformed calls, races and exceptions remains
unverified; the fake original in tests is not the vendor implementation.

The proxy first delegates initialization to the original. If the original
reports success, calls continue to the original. Otherwise it may offer the
fixed backend only after loading an exact-size alpha and checking its SHA-256.
The backend additionally requires an exact 1216 x 2688 packed RGB input and
SHA-256 before generating BGRA. A different input reports the null-result
branch and does not write a replacement image. If the mask or the exported
release-branch byte is missing, the original implementation remains selected.
An unavailable original library or missing required symbol raises
`UnsatisfiedLinkError` instead of reporting a successful initialization.
Resolution occurs on the first JNI call, however, so this exception can occur
later than the original class-loader error; SystemUI's handling is unverified.
If original initialization throws, the exception is left pending and the
previous fixed result is discarded; this is a conservative experiment, not a
claim that the target's behavior after a partial initialization is known.
The same applies when the subsequent original status or alpha query throws:
the fixed backend is not activated and the pending exception is preserved.

The real-mode constants identify only the already investigated theme:

- RGB SHA-256: `67ccd417f209d9d2904013b0158456997dd0db8f52f7e6806ed50540f421f636`.
- Raw single-channel alpha SHA-256: `019bfeea5f8e5109befa231f8febce04732320ff1e256e51fa28b092e6231c7a`.

The approved PNG resource, its provenance, and the limits of the offline mask
are documented in [README-alpha-resource.md](../aod/tools/README-alpha-resource.md).
Neither that PNG nor raw image data, a firmware library, a model, or a vendor
SDK is read from this source directory. During target import, EMU verifies and
decodes the PNG, then prepares a runtime copy as documented in the AOD record.
The raw alpha and original library paths are supplied explicitly by the build.
The loader rejects a final symbolic link, non-regular files and an unexpected
size, and avoids blocking on a FIFO. The import preparation assumes cooperative
ownership of its private tree, rather than defending against all same-UID races.

## Rebuilding and tests

The standalone SHA-256 vectors are described in [SHA256_TESTS.md](tests/SHA256_TESTS.md).
The target-path ARM64 build can be reproduced with NDK 28.2.13676358:

```sh
python native/gueststubs/aod-fixed/build_proxy.py --ndk /path/to/ndk --output /new/temporary/proxy.so
```

The builder refuses an existing output, checks the architecture, six JNI
exports, SONAME, direct dependencies and fixed Guest paths, and prints the
resulting SHA-256. The fixed paths are the inspected SystemUI APK's own
`!/lib/arm64-v8a/libzte_saliency.so` entry and
`/system_ext/priv-app/SystemUI_MFV/lib/arm64/rme_aod_mask.raw`. This build
does not supply a mask, install the result or establish the original library's
identity and ART namespace. Its artifact is still experimental.

To build and run the synthetic JNI tests on Linux with a C compiler, point to
an unmodified Android NDK public `jni.h`:

```sh
JNI_HEADER=/path/to/android-ndk/sysroot/usr/include/jni.h \
  sh native/gueststubs/aod-fixed/tests/run_proxy.sh
```

The tests compile a synthetic one-pixel backend, a fake original, and a mocked
JNI environment into a temporary directory. They exercise original delegation,
fixed matching, unknown input, exception preservation, missing data, and both
observed release branches. They also exercise status/alpha query exceptions,
a final symlink and a FIFO; synthetic vendor-init/constructor callbacks;
and controlled overlap of run/release, init/release and two init calls.
They do not execute vendor code or Android ART.
`RME_AOD_TEST_FIXTURE` is strictly test-only; it must never be packaged.

With the privately held offline RGB and raw alpha files already available,
an optional full-size test accepts their paths explicitly and compares all
output pixels; it does not print or add the supplied data to Git:

```sh
JNI_HEADER=/path/to/android-ndk/sysroot/usr/include/jni.h \
  sh native/gueststubs/aod-fixed/tests/run_real_input.sh /private/source.rgb /private/alpha.raw
```

Both scripts build temporary host executables. To cross-compile the real-mode
source for an offline ARM64 build check, supply **test-only** literal paths to
the compiler and write the result to a temporary directory:

```sh
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang" \
  -std=c11 -Wall -Wextra -Werror -fPIC -shared -Wl,--no-undefined \
  -Wl,-soname,libzte_saliency.so \
  -DRME_AOD_ORIGINAL_PATH='"/synthetic/original.so"' \
  -DRME_AOD_ALPHA_PATH='"/synthetic/alpha.raw"' \
  native/gueststubs/aod-fixed/proxy.c native/gueststubs/aod-fixed/sha256.c \
  -ldl -o /tmp/rme-aod-fixed-test.so
```

Those paths are deliberately nonfunctional. A successful build is not an
installation, Android namespace test, proof of a Guest input match, or visual
verification. The production asset is built with `build_proxy.py`, which fixes
the Guest paths and records its SHA-256 for the import-time check.
