# RedMagicEMU

RedMagicEMU es un proyecto independiente y no oficial basado en [AEmulator](https://github.com/uxazu/AEmulator), que a su vez se basa en el [emulador SENSES original](https://github.com/Istratii-tech/SENSES). No es un producto oficial de REDMAGIC ni está afiliado a REDMAGIC.

El proyecto tiene como objetivo usar como sistema invitado el firmware original del REDMAGIC NX809J con Android 16 / API 36 / ARM64. Esta restricción se aplica al firmware invitado, no al modelo del dispositivo anfitrión. La compatibilidad y el funcionamiento con este firmware aún no se han verificado en un dispositivo; consulta el [progreso](../PROGRESS.md) y la [línea base](../BASELINE.md).

## Compilación

El proyecto Android requiere Android SDK 36 y NDK r28. Para la compilación de referencia se utilizó JDK 21:

```shell
./gradlew :app:assembleStandardDebug
```

El desarrollo local usa el identificador de aplicación `app.redmagicemu`. Android la instala por separado de la aplicación original y sus datos privados no se migran automáticamente. Conserva la aplicación anterior y sus datos hasta que se haya verificado un método de migración.

## Créditos y licencia

RedMagicEMU se basa en AEmulator de [uxazu](https://github.com/uxazu), que surgió de los emuladores de HTC Desire HD y HTC One M7 del [autor original](https://t.me/istratii_tech1). Se reconocen su trabajo y los avisos existentes de derechos de autor y licencia.

El proyecto se distribuye bajo [GPL-3.0](../../LICENSE). Android, REDMAGIC, las demás marcas y el firmware pertenecen a sus respectivos propietarios.
