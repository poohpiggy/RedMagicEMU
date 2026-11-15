# Experimental SHA-256 check

This standalone helper is part of an unwired AOD experiment. It does not load
firmware or access a Guest. The implementation follows FIPS 180-4 section 6.2;
the project license applies. It is not a certified cryptographic module.

`rme_sha256` is a one-shot function. `out` must point to 32 writable bytes;
`data` may be null only for length zero, and the length must not exceed
`UINT64_MAX / 8`. The caller must check those preconditions. The tests cover
13 vectors including the empty message, `abc`, the standard multi-block vector,
padding boundaries, and one million `a` bytes.

Run on Linux with a C11 compiler:

```sh
sh native/gueststubs/aod-fixed/tests/run_sha256.sh
```

To compile with an Android NDK Linux ARM64 toolchain and run through QEMU,
supply the paths to an existing compatible Bionic linker and libraries:

```sh
RME_SHA_CC="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang" \
RME_SHA_QEMU=/path/to/qemu-aarch64 \
RME_SHA_LINKER=/path/to/android/linker64 \
RME_SHA_LIBDIR=/path/to/android/bionic/lib64 \
sh native/gueststubs/aod-fixed/tests/run_sha256.sh
```

The QEMU run verifies these vectors in the supplied runtime; it does not test
ART, the original SystemUI or the integration of this helper into the emulator.
No binaries, firmware or runtime components are stored in this directory.
