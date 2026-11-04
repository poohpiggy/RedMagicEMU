# RedMagicEMU

RedMagicEMU é um projeto independente e não oficial baseado no [AEmulator](https://github.com/uxazu/AEmulator), que por sua vez é baseado no [emulador SENSES](https://github.com/Istratii-tech/SENSES) original. Não é um produto oficial da REDMAGIC e não possui vínculo com a REDMAGIC.

O projeto tem como guest o firmware original REDMAGIC NX809J Android 16 / API 36 / ARM64. Essa restrição se aplica ao firmware guest, não ao modelo do dispositivo host. O suporte e a operação com esse firmware ainda não foram verificados em um dispositivo; consulte o [progresso](../PROGRESS.md) e a [linha de base](../BASELINE.md).

## Compilação

O projeto Android requer Android SDK 36 e NDK r28. A compilação de referência usou JDK 21:

```shell
./gradlew :app:assembleStandardDebug
```

O desenvolvimento local usa o application ID `app.redmagicemu`. É uma instalação Android separada do aplicativo upstream: seus dados privados não são migrados automaticamente. Mantenha o aplicativo antigo e seus dados até que um método de migração seja verificado.

## Créditos e licença

RedMagicEMU é baseado no AEmulator de [uxazu](https://github.com/uxazu), que evoluiu a partir dos emuladores HTC Desire HD e HTC One M7 do [autor original](https://t.me/istratii_tech1). O trabalho deles e os avisos existentes de direitos autorais e licença continuam reconhecidos.

O projeto é distribuído sob a [GPL-3.0](../../LICENSE). Android, REDMAGIC, outras marcas e firmware pertencem a seus respectivos proprietários.
