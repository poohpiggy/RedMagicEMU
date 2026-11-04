# RedMagicEMU

[العربية](docs/i18n/README.ar.md) · [Deutsch](docs/i18n/README.de.md) · [Español](docs/i18n/README.es.md) · [فارسی](docs/i18n/README.fa.md) · [Français](docs/i18n/README.fr.md) · [हिन्दी](docs/i18n/README.hi.md) · [Bahasa Indonesia](docs/i18n/README.id.md) · [Italiano](docs/i18n/README.it.md) · [日本語](docs/i18n/README.ja.md) · [한국어](docs/i18n/README.ko.md) · [Polski](docs/i18n/README.pl.md) · [Português (Brasil)](docs/i18n/README.pt-BR.md) · [Русский](docs/i18n/README.ru.md) · [Türkçe](docs/i18n/README.tr.md) · [Українська](docs/i18n/README.uk.md) · [Tiếng Việt](docs/i18n/README.vi.md) · [简体中文](docs/i18n/README.zh-CN.md)

RedMagicEMU is an unofficial, independently maintained project based on [AEmulator](https://github.com/uxazu/AEmulator), itself based on the original [SENSES emulator](https://github.com/Istratii-tech/SENSES). It is not an official REDMAGIC product and is not affiliated with REDMAGIC.

The project targets the original REDMAGIC NX809J Android 16 / API 36 / ARM64 firmware as its guest. This restriction applies to guest firmware, not the host device model. Support and operation with this firmware have not yet been verified on a device; see [progress](docs/PROGRESS.md) and [baseline](docs/BASELINE.md).

## Build

The Android project requires Android SDK 36 and NDK r28. The baseline build used JDK 21:

```shell
./gradlew :app:assembleStandardDebug
```

Local development uses application ID `app.redmagicemu`. It is a separate Android installation from the upstream app: its private data is not migrated automatically. Keep the old app and its data until a migration method has been verified.

## Credits and license

RedMagicEMU builds on AEmulator by [uxazu](https://github.com/uxazu), which grew out of the HTC Desire HD and HTC One M7 emulators by [the original author](https://t.me/istratii_tech1). Their work and the existing copyright and license notices remain acknowledged.

The project is distributed under [GPL-3.0](LICENSE). Android, REDMAGIC, other trademarks and firmware belong to their respective owners.
