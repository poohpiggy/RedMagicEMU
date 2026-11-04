# RedMagicEMU

RedMagicEMU adalah proyek independen tidak resmi berdasarkan [AEmulator](https://github.com/uxazu/AEmulator), yang berasal dari [emulator SENSES](https://github.com/Istratii-tech/SENSES). Ini bukan produk resmi REDMAGIC dan tidak berafiliasi dengan REDMAGIC.

Proyek ini menargetkan firmware asli REDMAGIC NX809J Android 16 / API 36 / ARM64 sebagai guest. Pembatasan ini berlaku untuk firmware guest, bukan model perangkat host. Dukungan dan pengoperasian belum diverifikasi pada perangkat; lihat [progres](../PROGRESS.md) dan [baseline](../BASELINE.md).

## Build

Proyek Android memerlukan Android SDK 36 dan NDK r28. Build baseline menggunakan JDK 21:

```shell
./gradlew :app:assembleStandardDebug
```

Pengembangan lokal menggunakan application ID `app.redmagicemu`. Instalasi Android ini terpisah dari aplikasi upstream: data privat tidak dimigrasikan otomatis. Simpan aplikasi lama dan datanya sampai metode migrasi terverifikasi.

## Kredit dan lisensi

RedMagicEMU berdasarkan AEmulator oleh [uxazu](https://github.com/uxazu), yang berasal dari emulator HTC Desire HD dan HTC One M7 oleh [penulis asli](https://t.me/istratii_tech1). Karya mereka serta pemberitahuan hak cipta dan lisensi tetap diakui.

Proyek ini didistribusikan berdasarkan [GPL-3.0](../../LICENSE). Android, REDMAGIC, merek dagang lain, dan firmware adalah milik pemilik masing-masing.
