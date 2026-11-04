# RedMagicEMU

RedMagicEMU ist ein inoffizielles, unabhängig gepflegtes Projekt auf Grundlage von [AEmulator](https://github.com/uxazu/AEmulator), das seinerseits auf dem ursprünglichen [SENSES-Emulator](https://github.com/Istratii-tech/SENSES) basiert. Es ist kein offizielles REDMAGIC-Produkt und steht nicht mit REDMAGIC in Verbindung.

Das Projekt zielt auf die originale REDMAGIC-NX809J-Firmware mit Android 16 / API 36 / ARM64 als Gast ab. Diese Einschränkung betrifft die Gast-Firmware, nicht das Modell des Host-Geräts. Unterstützung und Betrieb mit dieser Firmware wurden auf einem Gerät noch nicht überprüft; siehe [Fortschritt](../PROGRESS.md) und [Ausgangsbasis](../BASELINE.md).

## Build

Das Android-Projekt benötigt Android SDK 36 und NDK r28. Für den Build der Ausgangsbasis wurde JDK 21 verwendet:

```shell
./gradlew :app:assembleStandardDebug
```

Für die lokale Entwicklung wird die Anwendungs-ID `app.redmagicemu` verwendet. Android installiert die Anwendung getrennt von der Upstream-App; private Daten werden nicht automatisch migriert. Die alte App und ihre Daten sollten erhalten bleiben, bis ein Migrationsverfahren überprüft wurde.

## Mitwirkende und Lizenz

RedMagicEMU baut auf AEmulator von [uxazu](https://github.com/uxazu) auf, das aus den HTC-Desire-HD- und HTC-One-M7-Emulatoren des [ursprünglichen Autors](https://t.me/istratii_tech1) hervorging. Ihre Arbeit und die bestehenden Urheberrechts- und Lizenzhinweise werden anerkannt.

Das Projekt wird unter [GPL-3.0](../../LICENSE) vertrieben. Android, REDMAGIC, weitere Marken und Firmware gehören ihren jeweiligen Eigentümern.
