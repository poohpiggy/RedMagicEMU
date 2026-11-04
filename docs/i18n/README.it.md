# RedMagicEMU

RedMagicEMU è un progetto indipendente non ufficiale basato su [AEmulator](https://github.com/uxazu/AEmulator), a sua volta basato sull’[emulatore SENSES](https://github.com/Istratii-tech/SENSES) originale. Non è un prodotto ufficiale REDMAGIC e non è affiliato a REDMAGIC.

Il progetto ha come guest il firmware originale REDMAGIC NX809J Android 16 / API 36 / ARM64. Il limite riguarda il firmware guest, non il modello del dispositivo host. Supporto e funzionamento con tale firmware non sono ancora stati verificati su un dispositivo; vedere [avanzamento](../PROGRESS.md) e [baseline](../BASELINE.md).

## Compilazione

Il progetto Android richiede Android SDK 36 e NDK r28. La compilazione di baseline ha utilizzato JDK 21:

```shell
./gradlew :app:assembleStandardDebug
```

Lo sviluppo locale usa l’application ID `app.redmagicemu`. È un’installazione Android separata dall’app upstream: i dati privati non vengono migrati automaticamente. Conservare la vecchia app e i suoi dati finché non sarà verificato un metodo di migrazione.

## Riconoscimenti e licenza

RedMagicEMU si basa su AEmulator di [uxazu](https://github.com/uxazu), nato dagli emulatori HTC Desire HD e HTC One M7 dell’[autore originale](https://t.me/istratii_tech1). Il loro lavoro e le note esistenti su copyright e licenza restano riconosciuti.

Il progetto è distribuito sotto [GPL-3.0](../../LICENSE). Android, REDMAGIC, altri marchi e firmware appartengono ai rispettivi proprietari.
