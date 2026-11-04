# RedMagicEMU

RedMagicEMU پروژه‌ای مستقل و غیررسمی بر پایهٔ [AEmulator](https://github.com/uxazu/AEmulator) است که خود بر پایهٔ [شبیه‌ساز اصلی SENSES](https://github.com/Istratii-tech/SENSES) ساخته شده است. این پروژه محصول رسمی REDMAGIC نیست و وابستگی‌ای به REDMAGIC ندارد.

هدف پروژه استفاده از فرم‌ویر اصلی REDMAGIC NX809J با Android 16 / API 36 / ARM64 به‌عنوان مهمان است. این محدودیت برای فرم‌ویر مهمان است، نه مدل دستگاه میزبان. پشتیبانی و اجرای این فرم‌ویر هنوز روی دستگاه تأیید نشده است؛ [پیشرفت](../PROGRESS.md) و [خط مبنا](../BASELINE.md) را ببینید.

## ساخت

پروژهٔ Android به Android SDK 36 و NDK r28 نیاز دارد. برای ساخت خط مبنا از JDK 21 استفاده شد:

```shell
./gradlew :app:assembleStandardDebug
```

توسعهٔ محلی از شناسهٔ برنامهٔ `app.redmagicemu` استفاده می‌کند. این برنامه جدا از برنامهٔ بالادستی در Android نصب می‌شود و داده‌های خصوصی آن به‌طور خودکار منتقل نمی‌شوند. برنامهٔ قدیمی و داده‌های آن را تا تأیید روش انتقال نگه دارید.

## مشارکت‌کنندگان و مجوز

RedMagicEMU بر پایهٔ AEmulator اثر [uxazu](https://github.com/uxazu) است که از شبیه‌سازهای HTC Desire HD و HTC One M7 اثر [نویسندهٔ اصلی](https://t.me/istratii_tech1) پدید آمد. کار آنان و اعلان‌های موجود حق نشر و مجوز محفوظ و مورد تقدیر است.

پروژه تحت [GPL-3.0](../../LICENSE) منتشر می‌شود. Android، REDMAGIC، سایر علائم تجاری و فرم‌ویر متعلق به صاحبان مربوطه هستند.
