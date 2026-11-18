# Offline gfxstream Android Vulkan HAL entrypoint check

This runs the **existing upstream** `gfxstream_vk_android.cpp` entrypoint from
the local research checkout. It does not connect to a backend, access a device,
run an Android loader, or implement Vulkan. Three test-only entrypoint functions
return explicit failures or null; they are used to verify function pointer
identity, not to claim an instance or GPU works.

The original source is compiled as C for the Linux host test (only its common C
and C++ syntax is used). It is also compiled unchanged as C++ for an Android
ARM64 object with the NDK. The Android result is compile and symbol inspection
only: it is not linked, installed, or run. The substitute `util/macros.h` and
generated-entrypoint declarations are limited to visibility and the three
function signatures used by this file.

The tests check the module ID and tags, `vk0` open, and all three function
pointers. They also separately check whether invalid names return a normal
error and whether close succeeds. In the tested upstream build, `open` passes,
`invalid` fails because an assertion aborts its child, and `close` fails with
`-1`; the upstream close implementation does not free its allocation. Those
two checks are exploratory behavior checks, not a claim that the Android loader
necessarily calls either path. The runner reports failures and exits 1.

The source's SHA-256 is
`f35af525f7ffce9df016a65ceaa85a50946f5a6b1634aa6ed90f4275d24a7a60`.
The AOSP-derived `hwvulkan.h` and `hardware.h` in the cached Mesa Android stubs
have SHA-256 `c719f035ed301690d5688bf3378dde99ec398c4ceec1e5dce0983b4496a930e2`
and `05cc2f0108d6c87e36384380559943d34acd104e3c2ade9aeba4415cfb23aee8`.
These dependencies retain their own upstream copyright and licenses; they are
not copied into this directory. The stubs are not proof of the exact headers or
loader used by the target firmware.

From WSL, run:

```sh
sh /mnt/e/RedMagicEMU/tools/diagnostics/vulkan-hal-abi/run.sh
```

The default paths are the existing local research checkout and NDK r28c. Override
`GFXSTREAM_HAL_SOURCE`, `AOSP_STUB_INCLUDE`, or `ANDROID_NDK_LINUX` to use
equivalent local inputs. Output artifacts are placed in a new `/var/tmp` folder.
Compare hashes before treating different inputs as the same baseline.
