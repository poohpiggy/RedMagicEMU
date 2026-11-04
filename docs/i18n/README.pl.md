# RedMagicEMU

RedMagicEMU to nieoficjalny, niezależnie utrzymywany projekt oparty na [AEmulator](https://github.com/uxazu/AEmulator), który powstał na podstawie oryginalnego [emulatora SENSES](https://github.com/Istratii-tech/SENSES). Nie jest oficjalnym produktem REDMAGIC i nie jest powiązany z REDMAGIC.

Projekt jest przeznaczony dla oryginalnego firmware REDMAGIC NX809J Android 16 / API 36 / ARM64 jako systemu gościa. Ograniczenie dotyczy firmware gościa, a nie modelu urządzenia gospodarza. Obsługa i działanie z tym firmware nie zostały jeszcze sprawdzone na urządzeniu; zobacz [postępy](../PROGRESS.md) i [dokument bazowy](../BASELINE.md).

## Kompilacja

Projekt Android wymaga Android SDK 36 i NDK r28. W kompilacji bazowej użyto JDK 21:

```shell
./gradlew :app:assembleStandardDebug
```

W lokalnym środowisku programistycznym używany jest application ID `app.redmagicemu`. To instalacja Androida oddzielna od aplikacji źródłowej: prywatne dane nie są migrowane automatycznie. Zachowaj starą aplikację i jej dane do czasu zweryfikowania metody migracji.

## Autorzy i licencja

RedMagicEMU opiera się na AEmulator autorstwa [uxazu](https://github.com/uxazu), który rozwinął się z emulatorów HTC Desire HD i HTC One M7 [pierwotnego autora](https://t.me/istratii_tech1). Ich praca oraz istniejące informacje o prawach autorskich i licencji pozostają uznane.

Projekt jest rozpowszechniany na licencji [GPL-3.0](../../LICENSE). Android, REDMAGIC, pozostałe znaki towarowe i firmware należą do odpowiednich właścicieli.
