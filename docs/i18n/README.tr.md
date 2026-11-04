# RedMagicEMU

RedMagicEMU, [AEmulator](https://github.com/uxazu/AEmulator) temel alınarak bağımsız biçimde sürdürülen, resmî olmayan bir projedir. AEmulator da özgün [SENSES emülatörünü](https://github.com/Istratii-tech/SENSES) temel alır. Bu proje resmî bir REDMAGIC ürünü değildir ve REDMAGIC ile bağlantılı değildir.

Proje, konuk sistem olarak orijinal REDMAGIC NX809J Android 16 / API 36 / ARM64 ürün yazılımını hedefler. Bu kısıtlama ana cihazın modeline değil, konuk ürün yazılımına uygulanır. Bu ürün yazılımının desteği ve çalışması henüz bir cihazda doğrulanmamıştır; [ilerleme](../PROGRESS.md) ve [temel durum](../BASELINE.md) belgelerine bakın.

## Derleme

Android projesi Android SDK 36 ve NDK r28 gerektirir. Temel derlemede JDK 21 kullanılmıştır:

```shell
./gradlew :app:assembleStandardDebug
```

Yerel geliştirmede `app.redmagicemu` uygulama kimliği kullanılır. Android bunu kaynak uygulamadan ayrı yükler; özel veriler otomatik olarak taşınmaz. Taşıma yöntemi doğrulanana kadar eski uygulamayı ve verilerini koruyun.

## Katkılar ve lisans

RedMagicEMU, [uxazu](https://github.com/uxazu) tarafından geliştirilen AEmulator temel alınarak yapılmıştır. AEmulator, [özgün yazarın](https://t.me/istratii_tech1) HTC Desire HD ve HTC One M7 emülatörlerinden doğmuştur. Katkıları ve mevcut telif hakkı ve lisans bildirimleri korunmaktadır.

Proje [GPL-3.0](../../LICENSE) kapsamında dağıtılır. Android, REDMAGIC, diğer ticari markalar ve ürün yazılımları ilgili sahiplerine aittir.
