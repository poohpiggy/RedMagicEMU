<p align="center">
  <img src="docs/assets/redmagicemu-icon.svg" width="128" height="128" alt="RedMagicEMU placeholder icon">
</p>

<h1 align="center">RedMagicEMU</h1>

<p align="center"><strong>Unofficial, independently maintained Android firmware emulator</strong></p>

<p align="center" dir="ltr">
  <strong>English</strong> · <a href="docs/i18n/README.zh-CN.md">简体中文</a> · <a href="docs/i18n/README.ja.md">日本語</a> · <a href="docs/i18n/README.ko.md">한국어</a> · <a href="docs/i18n/README.vi.md">Tiếng Việt</a> · <a href="docs/i18n/README.id.md">Bahasa Indonesia</a><br>
  <a href="docs/i18n/README.de.md">Deutsch</a> · <a href="docs/i18n/README.fr.md">Français</a> · <a href="docs/i18n/README.es.md">Español</a> · <a href="docs/i18n/README.pt-BR.md">Português (Brasil)</a> · <a href="docs/i18n/README.it.md">Italiano</a> · <a href="docs/i18n/README.pl.md">Polski</a><br>
  <a href="docs/i18n/README.ru.md">Русский</a> · <a href="docs/i18n/README.uk.md">Українська</a> · <a href="docs/i18n/README.tr.md">Türkçe</a> · <a href="docs/i18n/README.hi.md">हिन्दी</a> · <a href="docs/i18n/README.ar.md">&#x2068;العربية&#x2069;</a> · <a href="docs/i18n/README.fa.md">&#x2068;فارسی&#x2069;</a>
</p>

RedMagicEMU is an unofficial, independently maintained project based on [AEmulator](https://github.com/uxazu/AEmulator), itself based on the original [SENSES emulator](https://github.com/Istratii-tech/SENSES). It is not an official REDMAGIC product and is not affiliated with REDMAGIC.

The project targets the original REDMAGIC NX809J Android 16 / API 36 / ARM64 firmware as its guest. This restriction applies to guest firmware, not the host device model. The firmware has reached its original desktop on one tested host. The project remains alpha, with known HAL limitations; see [progress](docs/PROGRESS.md) and [baseline](docs/BASELINE.md).

## Build

The Android project requires Android SDK 36 and NDK r28. The baseline build used JDK 21:

```shell
./gradlew :app:assembleStandardDebug
```

Local development uses application ID `app.redmagicemu`. It is a separate Android installation from the upstream app: its private data is not migrated automatically. Keep the old app and its data until a migration method has been verified.

## Credits and license

RedMagicEMU builds on AEmulator by [uxazu](https://github.com/uxazu), which grew out of the HTC Desire HD and HTC One M7 emulators by [the original author](https://t.me/istratii_tech1). Their work and the existing copyright and license notices remain acknowledged.

The project is distributed under [GPL-3.0](LICENSE). Android, REDMAGIC, other trademarks and firmware belong to their respective owners.
