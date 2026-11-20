# Removed legacy GB QEMU binary: retained provenance and notices

The legacy `app/src/main/jniLibs/arm64-v8a/libqemu_gb.so` was present in the
upstream AEmulator repository obtained from `https://github.com/uxazu/AEmulator`.
The earliest introduction recorded in this checkout is commit
`4b5e1663915ec72f26c11d3f0f5eac2022f2ca18`. Its SHA-256 is
`45887d675be42e862f0976c71208de7606da5a0cc46c79a3498529247f38a4aa`.
The original binary is preserved in Git history and in the local, Git-external
`before-legacy-prune` checkpoint documented in `MIGRATION-0.5.0.md`. No
equivalent complete rebuild source for this exact binary has been established.

The original binary contains the following version and copyright text:

```text
qemu-arm version 8.2.2
Copyright (c) 2003-2023 Fabrice Bellard and the QEMU Project developers
```

It also contains the following distinct XML notices, retained here verbatim:

```xml
<!-- Copyright (C) 2008 Free Software Foundation, Inc.

     Copying and distribution of this file, with or without modification,
     are permitted in any medium without royalty provided the copyright
     notice and this notice are preserved.  -->

<!-- Copyright (C) 2010-2020 Free Software Foundation, Inc.

     Copying and distribution of this file, with or without modification,
     are permitted in any medium without royalty provided the copyright
     notice and this notice are preserved.  -->

<!-- Copyright (C) 2021 Free Software Foundation, Inc.

     Copying and distribution of this file, with or without modification,
     are permitted in any medium without royalty provided the copyright
     notice and this notice are preserved.  -->

<!-- Copyright (C) 2021 Linaro Ltd.

     Copying and distribution of this file, with or without modification,
     are permitted in any medium without royalty provided the copyright
     notice and this notice are preserved.

     These are A/R profile VFP system registers. Debugger users probably
     don't really care about these, but because we used to (incorrectly)
     provide them to gdb in the org.gnu.gdb.arm.vfp XML we continue
     to do so via this separate XML.
     -->
```

These extracted notices do not establish the complete licensing or source
provenance of the prebuilt. The repository's original `LICENSE` and existing
upstream attribution remain in place. Other QEMU binaries are not removed by
the GB removal.

## Legacy KK QEMU binary

The later removed `app/src/main/jniLibs/arm64-v8a/libqemu_kk.so` was also
present in upstream AEmulator commit `4b5e1663915ec72f26c11d3f0f5eac2022f2ca18`.
Its SHA-256 is
`8fe3c6f31236bb5cd718e26e01bf325528eba7646fe56ec9f0b17c2582f8fd68`.
The original is preserved in Git history and the Git-external `before-kk-prune`
checkpoint. An equivalent complete rebuild source has not been established.
The binary reports:

```text
qemu-arm version 8.2.4
Copyright (c) 2003-2023 Fabrice Bellard and the QEMU Project developers
```

The four distinct embedded XML copyright notices match the notices reproduced
above byte for byte. Their inclusion here does not establish the full licensing
or source provenance of the prebuilt. The project `LICENSE` and existing
upstream attribution remain in place.

## ARM32 user-mode QEMU binary

The removed `app/src/main/jniLibs/arm64-v8a/libqemu_arm.so` was present in
the upstream AEmulator repository; its earliest addition in this checkout is
commit `3b97ed585e484395c455aca7338d7ddcbf955d9f`. The removed binary's
SHA-256 is `6c06ae5d15b90bb1cf5659e3bb1d7387810247d3e73350ba8b3324a577231fb4`.
The original is preserved in Git history and the Git-external
`before-20261115-legacy-native-batch` checkpoint; equivalent complete rebuild
source for this exact binary has not been established. Its embedded version
and attribution read:

```text
qemu-arm version 8.2.7
Copyright (c) 2003-2023 Fabrice Bellard and the QEMU Project developers
```

The four distinct embedded XML copyright notices match the notices reproduced
above byte for byte. These notices do not establish complete license or source
provenance; the project `LICENSE` and existing upstream attribution are kept.
