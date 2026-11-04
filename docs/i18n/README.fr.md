# RedMagicEMU

RedMagicEMU est un projet indépendant et non officiel fondé sur [AEmulator](https://github.com/uxazu/AEmulator), lui-même issu de l’[émulateur SENSES original](https://github.com/Istratii-tech/SENSES). Ce n’est pas un produit officiel REDMAGIC et il n’est pas affilié à REDMAGIC.

Le projet vise le firmware d’origine REDMAGIC NX809J sous Android 16 / API 36 / ARM64 comme système invité. Cette restriction concerne le firmware invité, et non le modèle de l’appareil hôte. La prise en charge et le fonctionnement avec ce firmware n’ont pas encore été vérifiés sur un appareil ; voir l’[avancement](../PROGRESS.md) et l’[état de référence](../BASELINE.md).

## Compilation

Le projet Android nécessite Android SDK 36 et NDK r28. La compilation de référence a utilisé JDK 21 :

```shell
./gradlew :app:assembleStandardDebug
```

Le développement local utilise l’identifiant d’application `app.redmagicemu`. Android l’installe séparément de l’application amont ; ses données privées ne sont pas migrées automatiquement. Conservez l’ancienne application et ses données jusqu’à la vérification d’une méthode de migration.

## Crédits et licence

RedMagicEMU repose sur AEmulator de [uxazu](https://github.com/uxazu), issu des émulateurs HTC Desire HD et HTC One M7 de l’[auteur original](https://t.me/istratii_tech1). Leur travail ainsi que les mentions existantes de droits d’auteur et de licence sont reconnus.

Le projet est distribué sous [GPL-3.0](../../LICENSE). Android, REDMAGIC, les autres marques et les firmwares appartiennent à leurs propriétaires respectifs.
